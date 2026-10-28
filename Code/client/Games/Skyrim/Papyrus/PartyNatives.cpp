#include <Events/PlayerDeathEvent.h>
#include <Events/CallEndedEvent.h>
#include <Events/PlayerProgressEvent.h>
#include <Papyrus/PartyNativesScope.h>
#include <Papyrus/BodyScripts.h>
#include <Papyrus/GlobalCall.h>
#include <Papyrus/GuestScripts.h>
#include <Papyrus/NativeReach.h>
#include <Papyrus/PlayerNatives.h>

#include <AI/Movement/PlayerControls.h>
#include <Games/EngineNames.h>
#include <Games/Skyrim/Interface/Menus/MessageBoxMenu.h>
#include <Forms/BGSLocation.h>
#include <Forms/TESObjectCELL.h>
#include <Actor.h>
#include <RTTI.h>
#include <Misc/GameVM.h>
#include <HookSwitch.h>
#include <Services/HostService.h>
#include <World.h>

#include <array>
#include <atomic>
#include <mutex>
#include <set>
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

uint32_t FormIdOf(const BSScript::Variable& acValue) noexcept
{
    auto* pObject = acValue.GetObject();
    return pObject ? BSScript::FormIdOfHandle(pObject->GetHandle()) : 0;
}

// Game.GetPlayer is rule 2's own (GuestScripts, HookGetPlayer).
bool IsGetPlayer(BSScript::IFunction* apFunction) noexcept
{
    const char* cpClass = apFunction->GetObjectTypeName().AsAscii();
    const char* cpName = apFunction->GetName().AsAscii();
    return cpClass && cpName && SameName(cpClass, "Game") && SameName(cpName, "GetPlayer");
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

// The latent party calls: this game's originals by their stack (the key their copies go by), and other games' copies
// here. Its lock is a leaf.
std::mutex s_latentLock;
std::set<uint32_t> s_originals;
PartyRules::LatentCopies s_copies;

// VirtualMachine::ReturnFromLatent (104807, CommonLibSSE-NG): a stack waiting on a latent call goes on with its result.
using TReturnFromLatent = void(BSScript::IVirtualMachine*, uint32_t aStackId, const BSScript::Variable& acValue);
TReturnFromLatent* RealReturnFromLatent = nullptr;

// An original's result goes to its copies' games (CallEndedEvent); a copy its player answered here is forgotten.
void HookReturnFromLatent(BSScript::IVirtualMachine* apThis, uint32_t aStackId, const BSScript::Variable& acValue)
{
    bool original = false;
    {
        std::scoped_lock _{s_latentLock};
        original = s_originals.erase(aStackId) != 0;
        s_copies.Returned(aStackId);
    }
    if (original)
        World::Get().GetRunner().Trigger(CallEndedEvent{aStackId, GlobalCall::Read(acValue).value_or(std::monostate{})});
    RealReturnFromLatent(apThis, aStackId, acValue);
}

// A copy waiting in aStack ends with acResult: the box it opened answered with it, else the stack returns it.
void EndWaiting(uint32_t aStack, const LocalValue& acResult) noexcept
{
    const auto* cpButton = std::get_if<int32_t>(&acResult);
    if (cpButton && MessageBoxes::AnswerStack(aStack, static_cast<uint8_t>(*cpButton)))
        return;
    auto* pVm = GameVM::Get() ? GameVM::Get()->virtualMachine : nullptr;
    if (!pVm || !pVm->IsWaitingOnLatent(aStack))
        return;
    BSScript::Variable result;
    GlobalCall::Write(result, acResult);
    pVm->ReturnFromLatent(aStack, result);
}

Hooks::Initializer s_hooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(68545, &RealToggleControls, HookToggleControls);
    HOOK_ID(104807, &RealReturnFromLatent, HookReturnFromLatent);
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

// The values a frame holds: its self, then its function's arguments.
template <class TVisit> void EachValue(BSScript::StackFrame& aFrame, TVisit&& aVisit) noexcept
{
    aVisit(aFrame.self);
    const uint32_t cCount = aFrame.pFunction ? aFrame.pFunction->GetParamCount() : 0;
    const uint32_t cPage = cCount ? aFrame.GetPageForFrame() : 0;
    for (uint32_t i = 0; i < cCount; ++i)
        if (const BSScript::Variable* pValue = aFrame.GetStackFrameVariable(i, cPage))
            aVisit(*pValue);
}

bool IsPlayer(uint32_t aFormId) noexcept
{
    return aFormId == PartyRules::kThisPlayerFormId || GuestScripts::IsGuestCharacter(aFormId);
}

// What a question measures from (party-calls.md, "Questions"): another reference in the call, else the asking
// scripts' own reference up the stack, else a location they name.
struct Asker
{
    TESObjectREFR* pReference = nullptr;
    BGSLocation* pLocation = nullptr;
};

Asker AskerOf(BSScript::Stack& aStack) noexcept
{
    Asker asker;
    const auto cNote = [&asker](const BSScript::Variable& acValue, bool aReferences) {
        const uint32_t cFormId = FormIdOf(acValue);
        TESForm* pForm = cFormId && !IsPlayer(cFormId) ? TESForm::GetById(cFormId) : nullptr;
        if (!pForm)
            return;
        if (auto* pReference = aReferences && !asker.pReference ? Cast<TESObjectREFR>(pForm) : nullptr)
            asker.pReference = pReference;
        else if (!asker.pLocation)
            asker.pLocation = Cast<BGSLocation>(pForm);
    };
    EachValue(*aStack.pTop, [&](const BSScript::Variable& acValue) { cNote(acValue, true); });
    for (BSScript::StackFrame* pFrame = aStack.pTop->pPrevious; pFrame; pFrame = pFrame->pPrevious)
    {
        cNote(pFrame->self, true);
        EachValue(*pFrame, [&](const BSScript::Variable& acValue) { cNote(acValue, false); });
    }
    return asker;
}

// Each player, measured from the asker: its distance in the same worldspace or interior, or whether it is in the
// location named.
std::vector<PartyRules::Asked> PlayersAsked(const Asker& acAsker) noexcept
{
    std::vector<uint32_t> players = GuestScripts::GuestCharacters();
    players.insert(players.begin(), PartyRules::kThisPlayerFormId);
    std::vector<PartyRules::Asked> asked;
    for (const uint32_t cPlayer : players)
    {
        auto* pActor = Cast<Actor>(TESForm::GetById(cPlayer));
        if (!pActor)
            continue;
        PartyRules::Asked entry{.Player = cPlayer};
        const TESObjectCELL* cpCell = pActor->parentCell;
        const TESObjectCELL* cpAskerCell = acAsker.pReference ? acAsker.pReference->parentCell : nullptr;
        if (cpCell && cpAskerCell &&
            (cpAskerCell->IsInterior() ? cpCell == cpAskerCell : !cpCell->IsInterior() && cpCell->worldspace == cpAskerCell->worldspace))
            entry.Distance = glm::distance(glm::vec3(pActor->position.x, pActor->position.y, pActor->position.z),
                                           glm::vec3(acAsker.pReference->position.x, acAsker.pReference->position.y,
                                                     acAsker.pReference->position.z));
        if (acAsker.pLocation)
            entry.InPlace = acAsker.pLocation->Holds(Cast<BGSLocation>(pActor->GetCurrentLocation()));
        asked.push_back(entry);
    }
    return asked;
}

PartyRules::CallFacts Scope::FactsOf(BSScript::IFunction* apFunction, const BSScript::Stack& acStack) const noexcept
{
    using PartyRules::Target;
    PartyRules::CallFacts facts{.Reach = NativeReach::CallOf(apFunction), .Cause = m_cause, .Story = m_story};
    facts.Hosting = World::Get().GetHostService().IsHosting();
    facts.Guest = PlayerNatives::IsGuest();
    facts.GuestCause = GuestScripts::IsGuestCharacter(m_cause);
    const bool cStatic = apFunction->GetIsStatic();
    // A global's player is this game's; a method's, its object if that is a player (a guest's: rule 2's swap).
    const uint32_t cSelf = cStatic ? PartyRules::kThisPlayerFormId : FormIdOf(acStack.pTop->self);
    facts.TargetId = cSelf;
    facts.Target = cSelf == PartyRules::kThisPlayerFormId     ? Target::kThisPlayer
                   : GuestScripts::IsGuestCharacter(cSelf) ? Target::kGuestCharacter
                                                           : Target::kNone;
    facts.OwnQuest = !cStatic && PlayerNatives::IsOwnQuest(cSelf);
    const BSScript::StackFrame* cpCaller = acStack.pTop->pPrevious;
    auto* pScript = cpCaller ? cpCaller->self.GetObject() : nullptr;
    facts.Body = BodyScripts::IsBody(pScript ? pScript->GetHandle() : 0);
    EachValue(*acStack.pTop, [&facts](const BSScript::Variable& acValue) {
        facts.PlayerInCall = facts.PlayerInCall || FormIdOf(acValue) == PartyRules::kThisPlayerFormId;
    });
    // Read from the code only where the rule asks: in a stack a guest's action ran (its lock is not on every call).
    facts.PlayerNative = facts.GuestCause && cStatic && !IsGetPlayer(apFunction) && NativeReach::ReachesThisPlayer(apFunction);
    return facts;
}

Scope::Scope(BSScript::IFunction* apFunction, BSScript::Stack* apStack, BSScript::IVirtualMachine* apVm, uint32_t aCause, bool aStory) noexcept
    : m_cause(aCause)
    , m_story(aStory)
{
    using PartyRules::Destination;
    if (!s_inSession.load(std::memory_order_relaxed) || !apStack || !apStack->pTop)
        return;
    const PartyRules::CallFacts cFacts = FactsOf(apFunction, *apStack);
    if (cFacts.Reach.Kind == NativeReach::Call::kControls)
    {
        m_controls = ControlsCall{PartyRules::HolderOf(aCause)};
        m_pOuter = std::exchange(t_pCall, &*m_controls);
    }
    m_progressed = cFacts.Reach.Kind == NativeReach::Call::kProgress;
    const PartyRules::Routing cRouting = PartyRules::RouteOf(cFacts);
    if (cRouting.Ask)
    {
        const bool cYesNo = apFunction->GetReturnType().type == BSScript::Variable::kBoolean;
        const auto cPicked = cYesNo ? std::nullopt : PartyRules::AskedPlayer(PlayersAsked(AskerOf(*apStack)));
        m_asks = PartyRules::PlayersToAsk(cYesNo, cPicked, GuestScripts::GuestCharacters());
        spdlog::debug("PartyNatives: {}.{} is asked of {} player(s), the first {:X}", apFunction->GetObjectTypeName().AsAscii(),
                      apFunction->GetName().AsAscii(), m_asks.size(), m_asks.front());
        return;
    }
    if (cRouting.To == Destination::kHere)
    {
        if (cFacts.Reach.Kind != NativeReach::Call::kNone && cFacts.Reach.Kind != NativeReach::Call::kWorld && !PartyRules::IsMirrored(aCause))
            spdlog::debug("PartyNatives: {}.{} is player {:X}'s moment", apFunction->GetObjectTypeName().AsAscii(), apFunction->GetName().AsAscii(), aCause);
        return;
    }

    auto call = GlobalCall::ReadCall(apFunction, *apStack->pTop);
    if (!call)
    {
        spdlog::warn("PartyNatives: {}.{}'s object or an argument is not one the session can carry: this game's only",
                     apFunction->GetObjectTypeName().AsAscii(), apFunction->GetName().AsAscii());
        return;
    }
    switch (cRouting.To)
    {
    case Destination::kHere: return;
    case Destination::kHost: m_result = PlayerNatives::SendToHost(apFunction, *apStack, std::move(*call)); return;
    case Destination::kCauseGame: m_result = PlayerNatives::SendToGuest(apFunction, *apStack, apVm, aCause, std::move(*call)); return;
    case Destination::kEveryGame: break;
    }
    if (cFacts.Reach.Kind == NativeReach::Call::kProgress)
        spdlog::info("PartyNatives: {}.{} on player {:X} is the party's progress", call->Class, call->Function, cFacts.TargetId);
    if (cRouting.Mark)
        PartyRules::MarkEachPlayer(*call, cRouting.MarkFor);
    if (cRouting.ThisPlayer)
        m_onThisPlayer = PartyRules::OnThisPlayer(*call);
    if (!cRouting.Original)
    {
        apStack->returnValue.Clear();
        m_result = BSScript::CallResult::kCompleted;
    }
    m_event = RoutedCallEvent{.To = CallTo::kOthers, .Call = std::move(*call)};
}

void Scope::Ran(BSScript::CallResult aResult, uint32_t aStackId) noexcept
{
    if (aResult != BSScript::CallResult::kInProgress)
        return;
    if (m_event)
    {
        std::scoped_lock _{s_latentLock};
        s_originals.insert(aStackId);
        m_event->ReplyId = aStackId;
        return;
    }
    if (!PartyRules::IsMirrored(m_cause))
        return;
    std::optional<LocalValue> ended;
    {
        std::scoped_lock _{s_latentLock};
        ended = s_copies.Started(PartyRules::PlayerOf(m_cause), aStackId);
    }
    // Its original already returned: ended once this frame lets it wait.
    if (ended)
        World::Get().GetRunner().Queue([aStackId, cResult = std::move(*ended)] { EndWaiting(aStackId, cResult); });
}

Scope::~Scope() noexcept
{
    if (m_progressed)
        World::Get().GetRunner().Trigger(PlayerProgressEvent{});
    if (m_controls)
    {
        t_pCall = m_pOuter;
        if (m_event && PartyRules::ShareOf(NativeReach::Call::kControls, m_cause, m_story, m_controls->Asked) != Share::kParty)
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

bool Run(const LocalCall& acCall, uint32_t aSource, uint32_t aKey) noexcept
{
    if (aKey)
    {
        std::scoped_lock _{s_latentLock};
        s_copies.Sent(aSource, aKey);
    }
    const GuestScripts::ScopedCause cMirrored(PartyRules::MirroredTag(aSource)); // the call it queues is tagged so (HookCallQueued)
    return GlobalCall::QueueCall(acCall);
}

void EndCopy(uint32_t aSource, uint32_t aKey, const LocalValue& acResult) noexcept
{
    std::optional<uint32_t> stack;
    {
        std::scoped_lock _{s_latentLock};
        stack = s_copies.Ended(aSource, aKey, acResult);
    }
    if (stack)
        EndWaiting(*stack, acResult);
}

void ForgetCopies(std::optional<uint32_t> aSource) noexcept
{
    std::scoped_lock _{s_latentLock};
    if (aSource)
        s_copies.Left(*aSource);
    else
        s_copies = {};
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
