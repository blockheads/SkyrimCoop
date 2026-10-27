#include <Papyrus/GuestScripts.h>
#include <Papyrus/NativeReach.h>
#include <Papyrus/PartyNativesScope.h>
#include <Papyrus/PlayerNatives.h>
#include <Papyrus/ScriptTags.h>

#include <Actor.h>
#include <Forms/TESForm.h>
#include <Games/EngineNames.h>
#include <Games/ActorExtension.h>
#include <HookSwitch.h>
#include <Misc/BSScript.h>
#include <Services/HostService.h>
#include <World.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

// On the host, scripts treat a guest's character as the player:
//   Rule 1: `x == Game.GetPlayer()` is true when x is a guest's character.
//   Rule 2: Game.GetPlayer() returns the guest whose action started the script, so every running script is
//   tagged with the guest it runs for. Evidence: docs/re/systems/papyrus-guest-scripts.md.
// On a guest the same tags mark the scripts its own player caused (WorldEvents lets only those run).
namespace GuestScripts
{
namespace
{
// --- What we hook ---

// Rule 1: a script compares two values (Internal::CodeTasklet, 105220).
using TCompare = int64_t(BSScript::CodeTasklet*, BSScript::Variable*, BSScript::Variable*);
TCompare* RealCompare = nullptr;
int64_t HookCompare(BSScript::CodeTasklet* apThis, BSScript::Variable* apFirst, BSScript::Variable* apSecond);

// Rule 2: Game.GetPlayer() returns (NativeFunction0<StaticFunctionTag, Actor*>::MarshallAndDispatch, 55618).
using TGetPlayer = bool(BSScript::IFunction*, BSScript::Variable*, BSScript::IVirtualMachine*, uint32_t, BSScript::Variable*, BSScript::StackFrame*);
TGetPlayer* RealGetPlayer = nullptr;
bool HookGetPlayer(BSScript::IFunction* apThis, BSScript::Variable* apBase, BSScript::IVirtualMachine* apVm, uint32_t aStackId, BSScript::Variable* apResult, BSScript::StackFrame* apFrame);

// Tagging: an event or call is queued on a script object (Internal::RawFuncCallQuery's constructor, 104943).
using TCallQueued = void*(void*, int32_t, void*, const BSFixedString*, const BSScript::Variable*, const BSScript::Arguments*);
TCallQueued* RealCallQueued = nullptr;
void* HookCallQueued(void* apThis, int32_t aCallType, void* apObjectType, const BSFixedString* apEvent, const BSScript::Variable* apSelf, const BSScript::Arguments* apArguments);

// Tagging: the call gets the stack that will run it (VirtualMachine, 104848).
using TStackCreated = bool(void*, BSScript::Stack*, void**);
TStackCreated* RealStackCreated = nullptr;
bool HookStackCreated(void* apThis, BSScript::Stack* apStack, void** appCall);

// Tagging: a script calls a native (NativeFunctionBase::Call, 104651).
using TNativeCalled = BSScript::CallResult(BSScript::IFunction*, BSScript::Stack**, void*, BSScript::IVirtualMachine*, bool);
TNativeCalled* RealNativeCalled = nullptr;
BSScript::CallResult HookNativeCalled(BSScript::IFunction* apThis, BSScript::Stack** appStack, void* apLogger, BSScript::IVirtualMachine* apVm, bool aArg4);

// Tagging: a script finished (Stack::~Stack, 104480).
using TStackEnded = void*(BSScript::Stack*);
TStackEnded* RealStackEnded = nullptr;
void* HookStackEnded(BSScript::Stack* apThis);

// A delayed native's work, queued for later (SkyrimScript::DelayFunctor, CommonLibSSE-NG).
struct DelayFunctor
{
    void* pVtable;     // 00
    uint32_t refCount; // 08
    uint32_t stackId;  // 0C: the stack that called the native
};
static_assert(offsetof(DelayFunctor, stackId) == 0x0C);

// Rule 2 for Spell.Cast (55747): it queues a CastFunctor (SkyrimVM, 53954), whose call (55744,
// `Variable operator()()`, its vtable slot 1) casts later, maybe after the script ended.
using TFunctorQueued = bool(void*, DelayFunctor**);
TFunctorQueued* RealFunctorQueued = nullptr;
bool HookFunctorQueued(void* apThis, DelayFunctor** appFunctor);

using TCastRun = BSScript::Variable*(DelayFunctor*, BSScript::Variable*);
TCastRun* RealCastRun = nullptr;
void* s_castRun = nullptr; // the original, as CastFunctor's vtable names it
BSScript::Variable* HookCastRun(DelayFunctor* apThis, BSScript::Variable* apResult);

// Ownership: ExtraDataList::SetOwnerForm (11609) ignores a temporary form, which a guest's character's base is.
using TSetOwnerForm = void(void*, TESForm*);
TSetOwnerForm* RealSetOwnerForm = nullptr;
void HookSetOwnerForm(void* apThis, TESForm* apOwner);

// Relationships: BGSRelationship between two NPC bases, set (24075, Actor.SetRelationshipRank 54759) and read
// (BGSRelationship::GetRelationship 24084). A guest's character's base is a temporary form.
using TSetRelationship = void*(TESForm*, TESForm*, int32_t);
TSetRelationship* RealSetRelationship = nullptr;
void* HookSetRelationship(TESForm* apFirst, TESForm* apSecond, int32_t aRank);
using TGetRelationship = void*(TESForm*, TESForm*);
TGetRelationship* RealGetRelationship = nullptr;
void* HookGetRelationship(TESForm* apFirst, TESForm* apSecond);

Hooks::Initializer s_hooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(105220, &RealCompare, HookCompare);
    HOOK_ID(55618, &RealGetPlayer, HookGetPlayer);
    HOOK_ID(104943, &RealCallQueued, HookCallQueued);
    HOOK_ID(104848, &RealStackCreated, HookStackCreated);
    HOOK_ID(104651, &RealNativeCalled, HookNativeCalled);
    HOOK_ID(104480, &RealStackEnded, HookStackEnded);
    HOOK_ID(53954, &RealFunctorQueued, HookFunctorQueued);
    HOOK_ID(55744, &RealCastRun, HookCastRun);
    HOOK_ID(11609, &RealSetOwnerForm, HookSetOwnerForm);
    HOOK_ID(24075, &RealSetRelationship, HookSetRelationship);
    HOOK_ID(24084, &RealGetRelationship, HookGetRelationship);
    // Hooks install later: this is still the engine's own address.
    s_castRun = reinterpret_cast<void*>(RealCastRun);
});

constexpr uint32_t cPlayerFormId = kThisPlayer;

bool IsHosting() noexcept
{
    return World::Get().GetHostService().IsHosting();
}

uint32_t FormIdOf(const BSScript::Variable& acValue) noexcept
{
    auto* pObject = acValue.GetObject();
    return pObject ? BSScript::FormIdOfHandle(pObject->GetHandle()) : 0;
}

// --- The guests' characters (SetGuest) ---

// By form id (0: free), read without engine calls: HookCallQueued runs with engine locks held.
std::array<std::atomic<uint32_t>, 16> s_guests{};

bool IsGuest(uint32_t aFormId) noexcept
{
    const auto cHolds = [aFormId](const auto& acSlot) { return acSlot.load(std::memory_order_relaxed) == aFormId; };
    return aFormId && std::ranges::any_of(s_guests, cHolds);
}

// The guest's character, confirmed with the engine (form ids are reused). Script threads only.
// this makes sure something isn't re-using our ID to be a funny cow "🐮" <- see image to the left
// for what a cow looks like you fucking retard
Actor* GuestCharacter(uint32_t aFormId) noexcept
{
    auto* pActor = IsGuest(aFormId) ? Cast<Actor>(TESForm::GetById(aFormId)) : nullptr;
    return pActor && pActor->GetExtension() && pActor->GetExtension()->IsOtherPlayer() ? pActor : nullptr;
}

// The first argument that is a guest's character, or 0.
uint32_t GuestArgument(const BSScript::Arguments* apArguments) noexcept
{
    if (!apArguments)
        return 0;
    const auto cFormIds = std::span(apArguments->pData, apArguments->size) | std::views::transform(FormIdOf);
    const auto it = std::ranges::find_if(cFormIds, IsGuest);
    return it != cFormIds.end() ? *it : 0;
}

// --- The player in a native's frame ---

// In a stack run for a guest, the player (0x14) held in a native's frame (its self, its arguments: a PlayerRef
// property, a stored player) is that guest's character for the call, as Game.GetPlayer() is (rule 2). The call copies
// what it reads (104651 copies self at its start), so the frame gets its own back after it.
class PlayerInFrame
{
  public:
    PlayerInFrame(BSScript::IFunction* apFunction, BSScript::StackFrame* apFrame, Actor* apGuest, BSScript::IVirtualMachine* apVm) noexcept
    {
        if (!apFrame || !apGuest)
            return;
        Take(&apFrame->self, apGuest, apVm);
        const uint32_t cCount = apFunction->GetParamCount();
        const uint32_t cPage = cCount ? apFrame->GetPageForFrame() : 0;
        for (uint32_t i = 0; i < cCount; ++i)
            Take(apFrame->GetStackFrameVariable(i, cPage), apGuest, apVm);
    }

    // The frame's player back.
    ~PlayerInFrame() noexcept
    {
        for (uint32_t i = 0; i < m_count; ++i)
        {
            BSScript::Variable guest; // takes the guest's character's reference, released as it goes
            guest.type = m_slots[i].pSlot->type;
            guest.data = m_slots[i].pSlot->data;
            m_slots[i].pSlot->type = m_slots[i].Type;
            m_slots[i].pSlot->data = m_slots[i].Data;
        }
    }

    PlayerInFrame(const PlayerInFrame&) = delete;
    PlayerInFrame& operator=(const PlayerInFrame&) = delete;

  private:
    static constexpr uint32_t kSlots = 17; // self and 16 arguments

    // A frame's variable that held the player, and that value, kept while the guest's character is in it.
    struct Slot
    {
        BSScript::Variable* pSlot;
        BSScript::Variable::Type Type;
        BSScript::Variable::Data Data;
    };

    void Take(BSScript::Variable* apSlot, Actor* apGuest, BSScript::IVirtualMachine* apVm) noexcept
    {
        if (!apSlot || m_count >= kSlots || FormIdOf(*apSlot) != cPlayerFormId)
            return;
        m_slots[m_count++] = {apSlot, apSlot->type, apSlot->data};
        BSScript::Variable guest;
        guest.SetActor(apGuest, apVm);
        apSlot->type = guest.type;
        apSlot->data = guest.data;
        guest.type = BSScript::Variable::kEmpty; // its reference is the frame's now
    }

    Slot m_slots[kSlots];
    uint32_t m_count = 0;
};

// --- Tags: which guest each call and stack runs for ---

Tags s_tags;

// The guest whose native is running on this thread: what the native queues runs for them too. So does its story.
thread_local uint32_t t_nativeGuest = 0;
thread_local bool t_nativeStory = false;
thread_local uint32_t t_nativeDepth = 0; // natives running on this thread
thread_local uint64_t t_nativeScript = 0; // the script object whose function called the running native

Cause CauseOf(const BSScript::Stack* apStack) noexcept
{
    return apStack && s_tags.Any() ? s_tags.OfStack(apStack->stackId) : Cause{};
}

uint32_t GuestOf(const BSScript::Stack* apStack) noexcept
{
    return CauseOf(apStack).Player;
}

// Whether an argument is this game's player: the engine's event of their action (an activation, a trigger, a hit).
bool PlayerArgument(const BSScript::Arguments* apArguments) noexcept
{
    if (!apArguments)
        return false;
    return std::ranges::any_of(std::span(apArguments->pData, apArguments->size),
                               [](const BSScript::Variable& acValue) { return FormIdOf(acValue) == cPlayerFormId; });
}

// --- The rules ---

int64_t HookCompare(BSScript::CodeTasklet* apThis, BSScript::Variable* apFirst, BSScript::Variable* apSecond)
{
    if (IsHosting())
    {
        const uint32_t cFirst = FormIdOf(*apFirst);
        const uint32_t cSecond = FormIdOf(*apSecond);
        if ((cFirst == cPlayerFormId && GuestCharacter(cSecond)) || (cSecond == cPlayerFormId && GuestCharacter(cFirst)))
            return 0; // equal
    }
    return RealCompare(apThis, apFirst, apSecond);
}

// 55618 serves every no-argument native returning an Actor: only GetPlayer is changed.
bool HookGetPlayer(BSScript::IFunction* apThis, BSScript::Variable* apBase, BSScript::IVirtualMachine* apVm, uint32_t aStackId, BSScript::Variable* apResult, BSScript::StackFrame* apFrame)
{
    const bool cResult = RealGetPlayer(apThis, apBase, apVm, aStackId, apResult, apFrame);
    const char* cpName = apThis->GetName().AsAscii();
    if (cResult && apFrame && apResult && cpName && SameName(cpName, "GetPlayer") && IsHosting())
        if (Actor* pGuest = GuestCharacter(GuestOf(apFrame->pParent)))
            apResult->SetActor(pGuest, apVm);
    return cResult;
}

// --- Tagging ---

// The call runs for the guest whose native queued it, whose character is an argument, or whose timer it answers. In a
// session, an engine event naming this game's player (no native queued it) runs for that player (PartyNatives).
void* HookCallQueued(void* apThis, int32_t aCallType, void* apObjectType, const BSFixedString* apEvent, const BSScript::Variable* apSelf, const BSScript::Arguments* apArguments)
{
    void* pResult = RealCallQueued(apThis, aCallType, apObjectType, apEvent, apSelf, apArguments);
    auto* pSelf = apSelf ? apSelf->GetObject() : nullptr;
    const QueuedCall cCall{.Native = {t_nativeGuest, t_nativeStory},
                           .NativeDepth = t_nativeDepth,
                           .InSession = PartyNatives::InSession(),
                           .Self = pSelf ? pSelf->GetHandle() : 0,
                           .Event = apEvent && apEvent->AsAscii() ? apEvent->AsAscii() : ""};
    const Cause cause = CauseOfCall(
        s_tags, cCall, [apArguments] { return GuestArgument(apArguments); }, [apArguments] { return PlayerArgument(apArguments); });
    if (cause.Player || s_tags.Any())
        s_tags.TagCall(apThis, cause);
    return pResult;
}

bool HookStackCreated(void* apThis, BSScript::Stack* apStack, void** appCall)
{
    if (apStack && appCall && *appCall && s_tags.Any())
        s_tags.MoveToStack(*appCall, apStack->stackId);
    return RealStackCreated(apThis, apStack, appCall);
}

// While a native runs for a guest, what it queues runs for them too; a timer it sets fires for them. The player in its
// frame is the guest's character. Where the call runs (here, the host's game, the guest's, every game) is
// PartyNatives::Scope's (PartyRules::RouteOf). A quest's stage set in a stack run for a player
// is the story: its fragment, and what that queues, run for that player as the story.
BSScript::CallResult HookNativeCalled(BSScript::IFunction* apThis, BSScript::Stack** appStack, void* apLogger, BSScript::IVirtualMachine* apVm, bool aArg4)
{
    BSScript::Stack* pStack = appStack ? *appStack : nullptr;
    Cause cause;
    uint32_t& guest = cause.Player;
    if (pStack && s_tags.Any())
    {
        cause = CauseOf(pStack);
        const char* cpName = apThis->GetName().AsAscii();
        if (auto* pScript = cpName && pStack->pTop ? pStack->pTop->self.GetObject() : nullptr)
            NativeCalled(s_tags, cpName, pScript->GetHandle(), cause);
    }
    Actor* pGuest = guest ? GuestCharacter(guest) : nullptr;
    const PlayerInFrame cPlayer(apThis, pGuest ? pStack->pTop : nullptr, pGuest, apVm);
    PartyNatives::Scope cRoute(apThis, pStack, apVm, guest, cause.Story);
    if (const auto cResult = cRoute.Result())
        return *cResult;
    const uint32_t cOuter = t_nativeGuest;
    const bool cOuterStory = t_nativeStory;
    const uint64_t cOuterScript = t_nativeScript;
    const Cause cWithin = Within(cause, NativeReach::CallOf(apThis).Kind == NativeReach::Call::kWorld);
    t_nativeGuest = cWithin.Player;
    t_nativeStory = cWithin.Story;
    const BSScript::StackFrame* cpCaller = pStack && pStack->pTop ? pStack->pTop->pPrevious : nullptr;
    auto* pCallerScript = cpCaller ? cpCaller->self.GetObject() : nullptr;
    t_nativeScript = pCallerScript ? pCallerScript->GetHandle() : 0;
    ++t_nativeDepth;
    // Rule 3: a question with no player's action is put to the players it asks, each in this game's player's place,
    // until a yes/no one says yes.
    BSScript::CallResult result{};
    if (cRoute.Asks().empty())
        result = RealNativeCalled(apThis, appStack, apLogger, apVm, aArg4);
    for (const uint32_t cAsked : cRoute.Asks())
    {
        Actor* pAsked = cAsked != cPlayerFormId ? GuestCharacter(cAsked) : nullptr;
        if (cAsked != cPlayerFormId && !pAsked)
            continue;
        const PlayerInFrame cInPlace(apThis, pAsked ? pStack->pTop : nullptr, pAsked, apVm);
        result = RealNativeCalled(apThis, appStack, apLogger, apVm, aArg4);
        const BSScript::Variable& cAnswer = pStack->returnValue;
        if (result != BSScript::CallResult::kCompleted || cAnswer.type != BSScript::Variable::kBoolean || cAnswer.data.b)
            break;
    }
    if (pStack)
        cRoute.Ran(result, pStack->stackId);
    --t_nativeDepth;
    t_nativeGuest = cOuter;
    t_nativeStory = cOuterStory;
    t_nativeScript = cOuterScript;
    return result;
}

bool HookFunctorQueued(void* apThis, DelayFunctor** appFunctor)
{
    DelayFunctor* pFunctor = appFunctor ? *appFunctor : nullptr;
    const bool cTagged = t_nativeGuest && pFunctor && static_cast<void**>(pFunctor->pVtable)[1] == s_castRun;
    if (cTagged)
        s_tags.TagFunctor(pFunctor, t_nativeGuest); // before: the game thread may run it at once
    const bool cQueued = RealFunctorQueued(apThis, appFunctor);
    if (cTagged && !cQueued)
        s_tags.TakeFunctor(pFunctor);
    return cQueued;
}

// The cast runs for the guest whose script called Spell.Cast: its effect on that guest is the host's world's.
BSScript::Variable* HookCastRun(DelayFunctor* apThis, BSScript::Variable* apResult)
{
    const uint32_t cOuter = t_nativeGuest;
    t_nativeGuest = s_tags.Any() ? s_tags.TakeFunctor(apThis) : 0;
    BSScript::Variable* pResult = RealCastRun(apThis, apResult);
    t_nativeGuest = cOuter;
    return pResult;
}

// A guest's character's base as an owner (a bed rented, a horse bought for Game.GetPlayer()) is the player's base:
// the party's, each game's own player (docs/design/party-model.md, "Items and gold"). Such a base is a temporary form.
// Never read through apOwner: a reference loading from its plugin (11595) passes the owner's form id, unresolved.
void HookSetOwnerForm(void* apThis, TESForm* apOwner)
{
    constexpr uint32_t cPlayerBaseFormId = 0x7;
    const auto cIsGuestBase = [apOwner](const std::atomic<uint32_t>& acSlot) {
        const Actor* pGuest = GuestCharacter(acSlot.load(std::memory_order_relaxed));
        return pGuest && pGuest->baseForm == apOwner;
    };
    if (apOwner && IsHosting() && std::ranges::any_of(s_guests, cIsGuestBase))
        apOwner = TESForm::GetById(cPlayerBaseFormId);
    RealSetOwnerForm(apThis, apOwner);
}

// A guest's character's base, as an NPC's relationship's other side, is the player's (0x7): the party's
// (docs/design/dragonborn.md, marriage). Only temporary bases are looked at: these run in NPC AI.
TESForm* PartyBase(TESForm* apBase) noexcept
{
    constexpr uint32_t cPlayerBaseFormId = 0x7;
    if (!apBase || !apBase->IsTemporary() || !IsHosting())
        return apBase;
    const auto cIsGuestBase = [apBase](const std::atomic<uint32_t>& acSlot) {
        const Actor* pGuest = GuestCharacter(acSlot.load(std::memory_order_relaxed));
        return pGuest && pGuest->baseForm == apBase;
    };
    return std::ranges::any_of(s_guests, cIsGuestBase) ? TESForm::GetById(cPlayerBaseFormId) : apBase;
}

void* HookSetRelationship(TESForm* apFirst, TESForm* apSecond, int32_t aRank)
{
    return RealSetRelationship(PartyBase(apFirst), PartyBase(apSecond), aRank);
}

void* HookGetRelationship(TESForm* apFirst, TESForm* apSecond)
{
    return RealGetRelationship(PartyBase(apFirst), PartyBase(apSecond));
}

void* HookStackEnded(BSScript::Stack* apThis)
{
    if (s_tags.Any())
        s_tags.EndStack(apThis->stackId);
    PlayerNatives::StackEnded(apThis->stackId);
    return RealStackEnded(apThis);
}
} // namespace

uint32_t NativeGuest() noexcept
{
    return t_nativeGuest;
}

bool InNative() noexcept
{
    return t_nativeDepth != 0;
}

uint64_t NativeScript() noexcept
{
    return t_nativeScript;
}

bool IsGuestCharacter(uint32_t aFormId) noexcept
{
    return IsHosting() && GuestCharacter(aFormId) != nullptr;
}

std::vector<uint32_t> GuestCharacters() noexcept
{
    std::vector<uint32_t> characters;
    if (IsHosting())
        for (const auto& cSlot : s_guests)
            if (const uint32_t cFormId = cSlot.load(std::memory_order_relaxed); cFormId && GuestCharacter(cFormId))
                characters.push_back(cFormId);
    return characters;
}

ScopedCause::ScopedCause(uint32_t aPlayer) noexcept
    : m_outer(t_nativeGuest)
{
    t_nativeGuest = aPlayer;
}

ScopedCause::~ScopedCause() noexcept
{
    t_nativeGuest = m_outer;
}

uint32_t RegisteredBy(uint64_t aObject, std::string_view aEvent) noexcept
{
    return s_tags.RegisteredBy(aObject, aEvent);
}

void SetGuest(uint32_t aFormId, bool aIsGuest) noexcept
{
    aIsGuest = aIsGuest && IsHosting(); // only the host's scripts run for guests
    for (auto& slot : s_guests)
        if (slot.load() == aFormId)
        {
            if (aIsGuest)
                return;
            slot.store(0);
        }
    if (!aIsGuest)
        return;
    for (auto& slot : s_guests)
        if (uint32_t expected = 0; slot.compare_exchange_strong(expected, aFormId))
            return;
    spdlog::warn("GuestScripts: no slot for guest {:X}", aFormId);
}
} // namespace GuestScripts
