#include <Events/PlayerDeathEvent.h>
#include <Papyrus/PartyNativesScope.h>
#include <Papyrus/BodyScripts.h>
#include <Papyrus/GlobalCall.h>
#include <Papyrus/GuestScripts.h>
#include <Papyrus/NativeReach.h>

#include <AI/Movement/PlayerControls.h>
#include <HookSwitch.h>
#include <Services/HostService.h>
#include <World.h>

#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

namespace PartyNatives
{
namespace
{
using PartyRules::Share;

static_assert(PartyRules::kMovement == BSInputEnableManager::kMovement && PartyRules::kLooking == BSInputEnableManager::kLooking &&
              PartyRules::kFighting == BSInputEnableManager::kFighting && PartyRules::kSneaking == BSInputEnableManager::kSneaking &&
              PartyRules::kMainFour == BSInputEnableManager::kMainFour);

std::atomic<bool> s_inSession{false};

// Who holds which controls here. Its lock is a leaf.
std::mutex s_lock;
PartyRules::ControlHolds s_holds;

// A downed player's hold (docs/design/death.md): what it may not do while it spectates; it looks and cycles on.
constexpr uint32_t kDeathHolder = 0xFFFFFFFE;
constexpr uint32_t kDeathControls = BSInputEnableManager::kMovement | BSInputEnableManager::kFighting | BSInputEnableManager::kActivate |
                                    BSInputEnableManager::kSneaking | BSInputEnableManager::kPOVSwitch | BSInputEnableManager::kJumping;
bool s_deathHeld = false;
uint32_t s_beforeDeath = 0; // which of them were enabled when it went down

// Whether a native changes a player's progression (PartyRules::ProgressOf), once per function: Scope asks on every call.
std::optional<PartyRules::Progress> ProgressOf(BSScript::IFunction* apFunction) noexcept
{
    static std::shared_mutex s_knownLock;
    static std::unordered_map<BSScript::IFunction*, std::optional<PartyRules::Progress>> s_known;
    {
        std::shared_lock _{s_knownLock};
        if (const auto it = s_known.find(apFunction); it != s_known.end())
            return it->second;
    }
    const char* cpClass = apFunction->GetObjectTypeName().AsAscii();
    const char* cpName = apFunction->GetName().AsAscii();
    const auto cProgress = cpClass && cpName ? PartyRules::ProgressOf(cpClass, cpName) : std::nullopt;
    std::unique_lock _{s_knownLock};
    s_known.emplace(apFunction, cProgress);
    return cProgress;
}

// The controls call running on this thread (Scope), none outside one.
thread_local ControlsCall* t_pCall = nullptr;

// ControlMap::ToggleControls (68545, CommonLibSSE-NG `ToggleControls(UEFlag, bool a_enable, bool a_storeState)`).
using TToggleControls = void(BSInputEnableManager*, uint32_t aFlags, bool aEnable, bool aStoreState);
TToggleControls* RealToggleControls = nullptr;

// aHolder takes the controls, or gives them back: what changes then is what no other holder still has.
uint32_t Hold(uint32_t aHolder, uint32_t aFlags, bool aEnable) noexcept
{
    std::scoped_lock _{s_lock};
    if (aEnable)
        return s_holds.GiveBack(aHolder, aFlags);
    s_holds.Take(aHolder, aFlags);
    return aFlags;
}

// In a script's controls call, the controls are held for its holder: giving back skips what another still holds.
void HookToggleControls(BSInputEnableManager* apThis, uint32_t aFlags, bool aEnable, bool aStoreState)
{
    ControlsCall* pCall = t_pCall;
    if (!pCall)
        return RealToggleControls(apThis, aFlags, aEnable, aStoreState);
    pCall->Asked |= aFlags;
    const uint32_t flags = Hold(pCall->Holder, aFlags, aEnable);
    if (flags != aFlags)
        spdlog::debug("PartyNatives: controls {:X} stay taken, another cutscene holds them", aFlags & ~flags);
    RealToggleControls(apThis, flags, aEnable, aStoreState);
    World::Get().GetRunner().Trigger(SpectatorRefreshEvent{});
}

Hooks::Initializer s_hooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(68545, &RealToggleControls, HookToggleControls);
});
} // namespace

void HoldDeathControls(bool aHold) noexcept
{
    auto* pControls = BSInputEnableManager::Get();
    if (!pControls || !RealToggleControls || aHold == s_deathHeld)
        return;
    // Given back: only what was enabled when it went down, and none a script still holds (a cutscene keeps its own).
    if (aHold)
        s_beforeDeath = pControls->enabledControls & kDeathControls;
    const uint32_t cChange = Hold(kDeathHolder, kDeathControls, !aHold) & (aHold ? kDeathControls : s_beforeDeath);
    s_deathHeld = aHold;
    RealToggleControls(pControls, cChange, !aHold, false);
}

bool ScriptControlsHeld() noexcept
{
    std::scoped_lock _{s_lock};
    return (s_holds.HeldExcept(kDeathHolder) & PartyRules::kMovement) != 0;
}

void SetInSession(bool aInSession) noexcept
{
    s_inSession = aInSession;
    BodyScripts::SetInSession(aInSession);
}

bool InSession() noexcept
{
    return s_inSession.load(std::memory_order_relaxed);
}

Scope::Scope(BSScript::IFunction* apFunction, BSScript::Stack* apStack, uint32_t aPlayer, bool aStory) noexcept
    : m_cause(aPlayer)
    , m_story(aStory)
{
    if (!s_inSession.load(std::memory_order_relaxed) || !apStack || !apStack->pTop)
        return;
    if (const auto cProgress = ProgressOf(apFunction))
    {
        Progress(apFunction, *apStack, *cProgress);
        return;
    }
    const NativeReach::Call cCall = NativeReach::CallOf(apFunction);
    if (cCall != NativeReach::Call::kControls && cCall != NativeReach::Call::kScreen && cCall != NativeReach::Call::kShow)
        return;
    if (cCall == NativeReach::Call::kControls)
    {
        m_controls = ControlsCall{PartyRules::HolderOf(aPlayer)};
        m_pOuter = std::exchange(t_pCall, &*m_controls);
    }
    const Share cWhose = PartyRules::Whose(aPlayer, aStory);
    switch (cCall == NativeReach::Call::kShow ? PartyRules::ShareOf(PartyRules::Kind::kShow, aPlayer, aStory, 0) : cWhose)
    {
    case Share::kHere: return; // another game's call
    case Share::kPlayer:
        spdlog::debug("PartyNatives: {}.{} is player {:X}'s moment", apFunction->GetObjectTypeName().AsAscii(), apFunction->GetName().AsAscii(), aPlayer);
        return;
    case Share::kParty: break;
    }

    auto call = GlobalCall::ReadCall(apFunction, *apStack->pTop);
    if (!call)
    {
        spdlog::warn("PartyNatives: {}.{}'s object or an argument is not one the session can carry: this game's only",
                     apFunction->GetObjectTypeName().AsAscii(), apFunction->GetName().AsAscii());
        return;
    }
    if (cWhose == Share::kParty)
        PartyRules::MarkEachPlayer(*call, aPlayer);
    m_event = PartyNativeEvent{std::move(*call)};
}

void Scope::Progress(BSScript::IFunction* apFunction, BSScript::Stack& aStack, const PartyRules::Progress& acProgress) noexcept
{
    auto call = GlobalCall::ReadCall(apFunction, *aStack.pTop);
    if (!call)
        return;
    // A global's player is this game's; a method's, its object if that is a player.
    const auto* cpSelf = std::get_if<LocalForm>(&call->Self);
    const uint32_t cPlayer = cpSelf ? cpSelf->FormId : 0x14;
    const PartyRules::Target cTarget = cPlayer == 0x14                         ? PartyRules::Target::kThisPlayer
                                       : GuestScripts::IsGuestCharacter(cPlayer) ? PartyRules::Target::kGuestCharacter
                                                                                 : PartyRules::Target::kNone;
    const BSScript::StackFrame* cpCaller = aStack.pTop->pPrevious;
    auto* pScript = cpCaller ? cpCaller->self.GetObject() : nullptr;
    const bool cBody = BodyScripts::IsBody(pScript ? pScript->GetHandle() : 0);
    const auto cRoute = PartyRules::RouteOf(acProgress, cTarget, m_cause, m_story, cBody, World::Get().GetHostService().IsHosting());
    if (!cRoute.Relay)
        return;
    spdlog::info("PartyNatives: {}.{} on player {:X} is the party's progress", call->Class, call->Function, cPlayer);
    PartyRules::MarkEachPlayer(*call, cPlayer);
    if (cRoute.ThisPlayer)
        m_onThisPlayer = PartyRules::OnThisPlayer(*call);
    m_skip = !cRoute.Original;
    m_event = PartyNativeEvent{std::move(*call)};
}

Scope::~Scope() noexcept
{
    if (m_controls)
    {
        t_pCall = m_pOuter;
        if (m_event && PartyRules::ShareOf(PartyRules::Kind::kControls, m_cause, m_story, m_controls->Asked) != Share::kParty)
        {
            spdlog::debug("PartyNatives: {}.{} leaves movement ({:X}): this game's only", m_event->Call.Class, m_event->Call.Function, m_controls->Asked);
            return;
        }
    }
    if (m_onThisPlayer && !Run(*m_onThisPlayer, 0))
        spdlog::warn("PartyNatives: {}.{} could not be queued on this game's player", m_onThisPlayer->Class, m_onThisPlayer->Function);
    if (m_event)
        World::Get().GetRunner().Trigger(std::move(*m_event));
}

bool Run(const LocalCall& acCall, uint32_t aSource) noexcept
{
    const GuestScripts::ScopedCause cMirrored(PartyRules::MirroredTag(aSource)); // the call it queues is tagged so (HookCallQueued)
    return GlobalCall::QueueCall(acCall);
}

void GiveBackControls(std::optional<uint32_t> aSource) noexcept
{
    std::vector<PartyRules::ControlHolds::Returned> returned;
    {
        std::scoped_lock _{s_lock};
        returned = s_holds.Leave(aSource ? std::optional(PartyRules::MirroredTag(*aSource)) : std::nullopt);
    }
    for (const auto& cReturned : returned)
    {
        const uint32_t cPlayer = PartyRules::PlayerOf(cReturned.Holder);
        spdlog::info("PartyNatives: player {}'s cutscene ended with controls {:X} taken here: {:X} come back", cPlayer, cReturned.Held, cReturned.Free);
        if (!cReturned.Free)
            continue;
        const auto c = PartyRules::EnableFor(cReturned.Free, cReturned.NoneElse);
        const std::array<LocalValue, 9> cArguments{c.Movement, c.Fighting, c.CamSwitch, c.Looking, c.Sneaking, c.Menu, c.Activate, c.Journal, int32_t{0}};
        const GuestScripts::ScopedCause cMirrored(cReturned.Holder); // that game's call: here only
        if (!GlobalCall::Queue("Game", "EnablePlayerControls", cArguments))
            spdlog::warn("PartyNatives: could not give the controls back");
    }
}
} // namespace PartyNatives
