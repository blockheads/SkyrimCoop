#include <Papyrus/PartyNativesScope.h>
#include <Papyrus/GlobalCall.h>
#include <Papyrus/GuestScripts.h>
#include <Papyrus/NativeReach.h>

#include <AI/Movement/PlayerControls.h>
#include <HookSwitch.h>
#include <World.h>

#include <array>
#include <atomic>
#include <mutex>
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

// The controls call running on this thread (Scope), none outside one.
thread_local ControlsCall* t_pCall = nullptr;

// ControlMap::ToggleControls (68545, CommonLibSSE-NG `ToggleControls(UEFlag, bool a_enable, bool a_storeState)`).
using TToggleControls = void(BSInputEnableManager*, uint32_t aFlags, bool aEnable, bool aStoreState);
TToggleControls* RealToggleControls = nullptr;

// In a script's controls call, the controls are held for its holder: giving back skips what another still holds.
void HookToggleControls(BSInputEnableManager* apThis, uint32_t aFlags, bool aEnable, bool aStoreState)
{
    ControlsCall* pCall = t_pCall;
    if (!pCall)
        return RealToggleControls(apThis, aFlags, aEnable, aStoreState);
    pCall->Asked |= aFlags;
    uint32_t flags = aFlags;
    {
        std::scoped_lock _{s_lock};
        if (aEnable)
            flags = s_holds.GiveBack(pCall->Holder, aFlags);
        else
            s_holds.Take(pCall->Holder, aFlags);
    }
    if (flags != aFlags)
        spdlog::debug("PartyNatives: controls {:X} stay taken, another cutscene holds them", aFlags & ~flags);
    RealToggleControls(apThis, flags, aEnable, aStoreState);
}

Hooks::Initializer s_hooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(68545, &RealToggleControls, HookToggleControls);
});
} // namespace

void SetInSession(bool aInSession) noexcept
{
    s_inSession = aInSession;
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
    const NativeReach::Call cCall = NativeReach::CallOf(apFunction);
    if (cCall != NativeReach::Call::kControls && cCall != NativeReach::Call::kScreen)
        return;
    if (cCall == NativeReach::Call::kControls)
    {
        m_controls = ControlsCall{PartyRules::HolderOf(aPlayer)};
        m_pOuter = std::exchange(t_pCall, &*m_controls);
    }
    switch (PartyRules::Whose(aPlayer, aStory))
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
