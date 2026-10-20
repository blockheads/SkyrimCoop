#include <TiltedOnlinePCH.h>
#include <HookSwitch.h>
#include "Commands.h"

#include <Forms/BGSBaseAlias.h>
#include <Forms/TESForm.h>
#include <Forms/TESQuest.h>
#include <Misc/BSScript.h>
#include <TESObjectREFR.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <MinHook.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <vector>

// Spike (docs/research/player-events.md, spike 1): what reaches world scripts and the story manager on a
// game, and which of it the player caused. Counts only: nothing is changed or forwarded.
namespace TestHarness::PlayerEvents
{
namespace
{
constexpr uint32_t cPlayerId = 0x14;
// The VM's type ids for objects that are not forms (CommonLibSSE-NG VMTYPEID).
constexpr uint32_t cRefAliasType = 140, cLocAliasType = 141, cActiveEffectType = 142;

struct Key
{
    std::string Event;
    uint64_t Handle; // what it is delivered to, resolved when read (no engine calls in the hook)
    bool PlayerArgument;
    auto operator<=>(const Key&) const = default;
};

struct Count
{
    uint64_t Calls = 0;
    std::set<uint32_t> Examples; // target form ids (the quest for an alias)
};

std::mutex s_lock;
std::map<Key, uint64_t> s_events;
std::map<uint32_t, uint64_t> s_storyEvents;
std::chrono::steady_clock::time_point s_since = std::chrono::steady_clock::now();

// Spike 2/5: a player context carried by cause. While a tagged stack runs a native, the calls it queues
// (an Activate's OnActivate, a SetStage's fragment) and the updates it registers inherit its player.
thread_local uint32_t t_cause = 0;
std::mutex s_tagLock;
std::map<std::pair<uint64_t, std::string>, uint32_t> s_pending; // (target handle, function) -> copy
std::map<uint32_t, uint32_t> s_stackTags;                        // stack id -> copy, 0 for none
bool s_traceCause = false;

// What BSScrapArray<Variable> looks like (CommonLibSSE-NG BSTArray.h): allocator, data, capacity, size.
struct ScrapVariables
{
    void* pAllocator = nullptr;
    BSScript::Variable* pData = nullptr;
    uint32_t Capacity = 0;
    uint32_t Pad14 = 0;
    uint32_t Size = 0;
    uint32_t Pad1C = 0;
};
static_assert(sizeof(ScrapVariables) == 0x20);

uint32_t FormIdOf(uint64_t aHandle)
{
    return static_cast<uint32_t>(aHandle & 0xFFFFFFFF);
}

// True if any of the (already built) arguments is the player.
bool HasPlayerArgument(const ScrapVariables* apArgs)
{
    for (uint32_t i = 0; apArgs && i < apArgs->Size; ++i)
        if (auto* pObject = apArgs->pData[i].GetObject(); pObject && FormIdOf(pObject->GetHandle()) == cPlayerId)
            return true;
    return false;
}

// What the event is delivered to, and the form to name it by.
std::pair<std::string, uint32_t> Classify(uint64_t aHandle)
{
    auto* pPolicy = BSScript::IObjectHandlePolicy::Get();
    if (!pPolicy)
        return {"unknown", 0};
    if (pPolicy->HandleIsType(cRefAliasType, aHandle))
    {
        auto* pAlias = static_cast<BGSBaseAlias*>(pPolicy->GetObjectForHandle(cRefAliasType, aHandle));
        if (!pAlias || !pAlias->owningQuest)
            return {"alias", 0};
        auto* pRef = pAlias->owningQuest->GetAliasedRef(pAlias->aliasId);
        return {pRef && pRef->formID == cPlayerId ? "player alias" : "alias", pAlias->owningQuest->formID};
    }
    if (pPolicy->HandleIsType(cLocAliasType, aHandle))
        return {"location alias", 0};
    if (pPolicy->HandleIsType(cActiveEffectType, aHandle))
        return {"effect", 0};
    const uint32_t cFormId = FormIdOf(aHandle);
    if (cFormId == cPlayerId)
        return {"player", cFormId};
    auto* pForm = TESForm::GetById(cFormId);
    return {pForm ? std::to_string(static_cast<int>(pForm->formType)) : "unknown", cFormId};
}

// IVirtualMachine::SendEvent (vfunc 0x24): every script event, the engine's and SKSE plugins', goes here,
// with or without a script attached to its target. Counted only: it can run with engine locks held.
TP_THIS_FUNCTION(TSendEvent, void, BSScript::IVirtualMachine, uint64_t, const BSFixedString&, BSScript::IFunctionArguments*);
TSendEvent* RealSendEvent = nullptr;
uint64_t s_sent = 0;

void TP_MAKE_THISCALL(HookSendEvent, BSScript::IVirtualMachine, uint64_t aHandle, const BSFixedString& acName, BSScript::IFunctionArguments* apArgs)
{
    {
        std::scoped_lock _{s_lock};
        ++s_sent;
    }
    TiltedPhoques::ThisCall(RealSendEvent, apThis, aHandle, acName, apArgs);
}

// BSScript::Internal::RawFuncCallQuery's constructor (104943): a call queued on a script object, with its
// arguments already built. DispatchMethodCall1 (vfunc 0x27), which SendEvent makes for each script attached
// to the target, builds one once the script has a handler of that name.
TP_THIS_FUNCTION(TRawFuncCallQuery, void*, void, int32_t, void*, const BSFixedString*, const BSScript::Variable*, const ScrapVariables*);
TRawFuncCallQuery* RealRawFuncCallQuery = nullptr;

void* TP_MAKE_THISCALL(HookRawFuncCallQuery, void, int32_t aCallType, void* apClass, const BSFixedString* apName,
                       const BSScript::Variable* apSelf, const ScrapVariables* apArgs)
{
    const char* cpName = apName ? apName->AsAscii() : nullptr;
    if (t_cause && cpName && apSelf)
    {
        if (auto* pObject = apSelf->GetObject())
        {
            const uint64_t cHandle = pObject->GetHandle();
            {
                std::scoped_lock _{s_tagLock};
                s_pending[{cHandle, cpName}] = t_cause;
            }
            if (s_traceCause)
                spdlog::info("Player context: {} on {:X} queued for {:X}", cpName, FormIdOf(cHandle), t_cause);
        }
    }
    if (cpName && cpName[0] == 'O' && cpName[1] == 'n' && apSelf)
    {
        if (auto* pObject = apSelf->GetObject())
        {
            const uint64_t cHandle = pObject->GetHandle();
            const bool cPlayer = HasPlayerArgument(apArgs);
            std::scoped_lock _{s_lock};
            ++s_events[Key{cpName, cHandle, cPlayer}];
        }
    }
    return TiltedPhoques::ThisCall(RealRawFuncCallQuery, apThis, aCallType, apClass, apName, apSelf, apArgs);
}

// BGSStoryEventManager::AddEvent_Impl (32359): every story event, by its type index (QuestEvents.h).
TP_THIS_FUNCTION(TAddEvent, uint32_t, void, uint32_t, const void*);
TAddEvent* RealAddEvent = nullptr;

// Spike 4: the location change story event (CLOC), as PlayerCharacter::SetParentCell builds it (32288).
struct LocationChangeEvent
{
    uint32_t ActorHandle;  // 00
    uint32_t Pad04;
    TESForm* pOldLocation; // 08
    TESForm* pNewLocation; // 10
};
static_assert(sizeof(LocationChangeEvent) == 0x18);
bool s_traceStory = false;
std::vector<std::pair<uint32_t, uint32_t>> s_capturedLocationChanges; // (old, new) the player raised

uint32_t LocationChangeIndex()
{
    POINTER_SKYRIMSE(uint32_t, s_index, 380069); // the CLOC event type's registered index
    return *s_index.Get();
}

uint32_t TP_MAKE_THISCALL(HookAddEvent, void, uint32_t aIndex, const void* apEvent)
{
    {
        std::scoped_lock _{s_lock};
        ++s_storyEvents[aIndex];
        if (s_traceStory && apEvent && aIndex == LocationChangeIndex())
        {
            auto* pEvent = static_cast<const LocationChangeEvent*>(apEvent);
            s_capturedLocationChanges.emplace_back(pEvent->pOldLocation ? pEvent->pOldLocation->formID : 0,
                                                   pEvent->pNewLocation ? pEvent->pNewLocation->formID : 0);
        }
    }
    return TiltedPhoques::ThisCall(RealAddEvent, apThis, aIndex, apEvent);
}

// Spike 2: a player context. In a script stack started by an event that has a remote player's copy among
// its arguments, Game.GetPlayer() returns that copy, so `akActionRef == Game.GetPlayer()` passes for it.
bool s_contextOn = false;
std::atomic<uint32_t> s_contextCopy = 0;
bool s_traceGetPlayer = false;
uint64_t s_contextHits = 0;

// BSScript::StackFrame (CommonLibSSE-NG StackFrame.h).
struct Frame
{
    BSScript::Stack* pParent;     // 00
    Frame* pPrevious;             // 08
    BSScript::IFunction* pFunction; // 10
    void* pObjectType;            // 18
    uint32_t InstructionPointer;  // 20
    uint32_t Pad24;
    BSScript::Variable Self;      // 28
};
static_assert(offsetof(Frame, pPrevious) == 0x8 && offsetof(Frame, pFunction) == 0x10 && offsetof(Frame, Self) == 0x28);

// BSScript::Stack (CommonLibSSE-NG Stack.h): the top frame and the stack's id.
struct StackView
{
    uint8_t Pad00[0x60];
    Frame* pTop;       // 60
    uint8_t Pad68[0x18];
    uint32_t StackId;  // 80
};
static_assert(offsetof(StackView, pTop) == 0x60 && offsetof(StackView, StackId) == 0x80);

Frame* Bottom(Frame* apFrame)
{
    while (apFrame && apFrame->pPrevious)
        apFrame = apFrame->pPrevious;
    return apFrame;
}

// The remote player's copy among the arguments of the event handler at the bottom of the stack.
uint32_t ArgumentCopy(Frame* apBottom)
{
    const char* cpName = apBottom->pFunction->GetName().AsAscii();
    if (!cpName || cpName[0] != 'O' || cpName[1] != 'n')
        return 0;
    auto* pFrame = reinterpret_cast<BSScript::StackFrame*>(apBottom);
    const uint32_t cPage = apBottom->pParent->GetPageForFrame(pFrame);
    for (uint32_t i = 0, count = apBottom->pFunction->GetParamCount(); i < count; ++i)
    {
        auto* pVariable = apBottom->pParent->GetStackFrameVariable(pFrame, i, cPage);
        auto* pObject = pVariable ? pVariable->GetObject() : nullptr;
        auto* pActor = pObject ? Cast<Actor>(TESForm::GetById(FormIdOf(pObject->GetHandle()))) : nullptr;
        if (pActor && pActor->GetExtension() && pActor->GetExtension()->IsOtherPlayer())
            return pActor->formID;
    }
    return 0;
}

// The player a stack runs for: inherited from what queued its bottom call, else its event's arguments.
uint32_t StackTag(Frame* apFrame)
{
    Frame* pBottom = Bottom(apFrame);
    if (!pBottom || !pBottom->pFunction || !pBottom->pParent)
        return 0;
    const uint32_t cStackId = reinterpret_cast<StackView*>(pBottom->pParent)->StackId;
    {
        std::scoped_lock _{s_tagLock};
        if (auto it = s_stackTags.find(cStackId); it != s_stackTags.end())
            return it->second;
    }
    uint32_t copy = 0;
    auto* pSelf = pBottom->Self.GetObject();
    const char* cpName = pBottom->pFunction->GetName().AsAscii();
    if (pSelf && cpName)
    {
        std::scoped_lock _{s_tagLock};
        if (auto it = s_pending.find({pSelf->GetHandle(), cpName}); it != s_pending.end())
        {
            copy = it->second;
            s_pending.erase(it);
        }
    }
    if (copy && s_traceCause)
        spdlog::info("Player context: stack {} ({} on {:X}) inherits {:X}", cStackId, cpName, FormIdOf(pSelf->GetHandle()), copy);
    if (!copy)
        copy = ArgumentCopy(pBottom);
    std::scoped_lock _{s_tagLock};
    s_stackTags[cStackId] = copy;
    return copy;
}

Actor* ContextPlayer(Frame* apFrame)
{
    const uint32_t cCopy = StackTag(apFrame);
    return cCopy ? Cast<Actor>(TESForm::GetById(cCopy)) : nullptr;
}

// NativeFunctionBase::Call (104651), every native call: what the native queues inherits the stack's player.
// RegisterForSingleUpdate's later OnUpdate does too.
TP_THIS_FUNCTION(TCall, uint32_t, BSScript::IFunction, StackView**, void*, BSScript::IVirtualMachine*, bool);
TCall* RealCall = nullptr;

uint32_t TP_MAKE_THISCALL(HookCall, BSScript::IFunction, StackView** appStack, void* apLogger, BSScript::IVirtualMachine* apVm, bool aArg)
{
    uint32_t cause = 0;
    if (s_contextOn && appStack && *appStack && (*appStack)->pTop)
    {
        cause = StackTag((*appStack)->pTop);
        const char* cpName = cause ? apThis->GetName().AsAscii() : nullptr;
        if (cpName && std::strncmp(cpName, "RegisterForSingleUpdate", 23) == 0)
        {
            if (auto* pTarget = (*appStack)->pTop->Self.GetObject())
            {
                const char* cpEvent = std::strcmp(cpName, "RegisterForSingleUpdateGameTime") == 0 ? "OnUpdateGameTime" : "OnUpdate";
                {
                    std::scoped_lock _{s_tagLock};
                    s_pending[{pTarget->GetHandle(), cpEvent}] = cause;
                }
                if (s_traceCause)
                    spdlog::info("Player context: {} on {:X} registered for {:X}", cpEvent, FormIdOf(pTarget->GetHandle()), cause);
            }
        }
    }
    const uint32_t cPrevious = t_cause;
    t_cause = cause;
    const uint32_t cResult = TiltedPhoques::ThisCall(RealCall, apThis, appStack, apLogger, apVm, aArg);
    t_cause = cPrevious;
    return cResult;
}

// NativeFunction0<StaticFunctionTag, Actor*>::MarshallAndDispatch (55618): Game.GetPlayer and other
// no-argument natives returning an Actor. It packs the C function's Actor into the result.
TP_THIS_FUNCTION(TMarshall, bool, BSScript::IFunction, BSScript::Variable*, BSScript::IVirtualMachine*, uint32_t, BSScript::Variable*, Frame*);
TMarshall* RealMarshall = nullptr;

bool TP_MAKE_THISCALL(HookMarshall, BSScript::IFunction, BSScript::Variable* apBase, BSScript::IVirtualMachine* apVm, uint32_t aStackId,
                      BSScript::Variable* apResult, Frame* apFrame)
{
    const bool cResult = TiltedPhoques::ThisCall(RealMarshall, apThis, apBase, apVm, aStackId, apResult, apFrame);
    const char* cpName = s_contextOn ? apThis->GetName().AsAscii() : nullptr;
    if (cpName && std::strcmp(cpName, "GetPlayer") == 0 && apFrame && apResult)
    {
        Actor* pCopy = ContextPlayer(apFrame);
        if (!pCopy && s_traceGetPlayer)
        {
            Frame* pBottom = apFrame;
            while (pBottom->pPrevious)
                pBottom = pBottom->pPrevious;
            auto* pSelf = pBottom->Self.GetObject();
            spdlog::info("Player context: GetPlayer untagged in {} on {:X} (stack {})", pBottom->pFunction ? pBottom->pFunction->GetName().AsAscii() : "?",
                         pSelf ? FormIdOf(pSelf->GetHandle()) : 0, aStackId);
        }
        if (pCopy)
        {
            // The packer (55510) the native uses, then Variable's copy (104297).
            TP_THIS_FUNCTION(TPack, void, BSScript::Variable, Actor*, BSScript::IVirtualMachine*);
            TP_THIS_FUNCTION(TAssign, void, BSScript::Variable, const BSScript::Variable*);
            POINTER_SKYRIMSE(TPack, s_pack, 55510);
            POINTER_SKYRIMSE(TAssign, s_assign, 104297);
            BSScript::Variable packed;
            packed.type = BSScript::Variable::kEmpty;
            packed.data.pObj = nullptr;
            TiltedPhoques::ThisCall(s_pack, &packed, pCopy, apVm);
            TiltedPhoques::ThisCall(s_assign, apResult, &packed);
            Frame* pBottom = apFrame;
            while (pBottom->pPrevious)
                pBottom = pBottom->pPrevious;
            auto* pSelf = pBottom->Self.GetObject();
            auto* pPacked = apResult->GetObject();
            s_contextCopy = pCopy->formID;
            spdlog::info("Player context: GetPlayer is {:X} in {} on {:X} (stack {}), packed type {} handle {:X}", pCopy->formID,
                         pBottom->pFunction->GetName().AsAscii(), pSelf ? FormIdOf(pSelf->GetHandle()) : 0, aStackId,
                         static_cast<uint64_t>(apResult->type), pPacked ? pPacked->GetHandle() : 0);
            std::scoped_lock _{s_lock};
            ++s_contextHits;
        }
    }
    return cResult;
}

// The VM's comparison of two values (105220). Logged when one side is the copy GetPlayer last returned.
TP_THIS_FUNCTION(TCompare, int64_t, void, BSScript::Variable*, BSScript::Variable*);
TCompare* RealCompare = nullptr;

// Its `this` is the running CodeTasklet (CommonLibSSE-NG CodeTasklet.h), which holds the stack.
struct TaskletView
{
    uint8_t Pad00[0x10];
    StackView* pStack; // 10
};
static_assert(offsetof(TaskletView, pStack) == 0x10);

int64_t TP_MAKE_THISCALL(HookCompare, void, BSScript::Variable* apFirst, BSScript::Variable* apSecond)
{
    int64_t cResult = TiltedPhoques::ThisCall(RealCompare, apThis, apFirst, apSecond);
    if (!s_contextOn || cResult == 0)
        return cResult;
    auto* pFirst = apFirst->GetObject();
    auto* pSecond = apSecond->GetObject();
    const uint64_t cFirst = pFirst ? pFirst->GetHandle() : 0;
    const uint64_t cSecond = pSecond ? pSecond->GetHandle() : 0;
    // Scoped: in a stack running for a remote player, the player compares equal to that player's copy.
    if (pFirst && pSecond && (FormIdOf(cFirst) == cPlayerId || FormIdOf(cSecond) == cPlayerId))
    {
        auto* pStack = static_cast<TaskletView*>(apThis)->pStack;
        const uint32_t cTag = pStack && pStack->pTop ? StackTag(pStack->pTop) : 0;
        const uint32_t cOther = FormIdOf(cFirst) == cPlayerId ? FormIdOf(cSecond) : FormIdOf(cFirst);
        if (cTag && cOther == cTag)
        {
            spdlog::info("Player context: compare player with {:X} in stack {} = equal (scoped)", cTag, pStack->StackId);
            return 0;
        }
    }
    const uint32_t cCopy = s_contextCopy;
    if (!cCopy)
        return cResult;
    if (FormIdOf(cFirst) == cCopy || FormIdOf(cSecond) == cCopy)
        spdlog::info("Player context: compare {:X} ({}) with {:X} ({}) = {}", cFirst, static_cast<uint64_t>(apFirst->type), cSecond,
                     static_cast<uint64_t>(apSecond->type), cResult);
    return cResult;
}

TiltedPhoques::Initializer s_playerEventHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(void*, s_vmVTable, 252631); // BSScript::Internal::VirtualMachine's vtable
        POINTER_SKYRIMSE(TAddEvent, s_addEvent, 32359);
        POINTER_SKYRIMSE(TRawFuncCallQuery, s_rawFuncCallQuery, 104943);
        POINTER_SKYRIMSE(TMarshall, s_marshall, 55618);
        RealSendEvent = reinterpret_cast<TSendEvent*>(s_vmVTable.Get()[0x24]);
        RealAddEvent = s_addEvent.Get();
        RealRawFuncCallQuery = s_rawFuncCallQuery.Get();
        RealMarshall = s_marshall.Get();
        POINTER_SKYRIMSE(TCall, s_call, 104651);
        RealCall = s_call.Get();
        TP_HOOK(&RealCall, HookCall);
        POINTER_SKYRIMSE(TCompare, s_compare, 105220);
        RealCompare = s_compare.Get();
        TP_HOOK(&RealCompare, HookCompare);
        TP_HOOK(&RealSendEvent, HookSendEvent);
        TP_HOOK(&RealRawFuncCallQuery, HookRawFuncCallQuery);
        TP_HOOK(&RealMarshall, HookMarshall);
        TP_HOOK(&RealAddEvent, HookAddEvent);
    });

constexpr const char* cStoryEventNames[] = {"KILL", "ASSU", "CLOC", "SCPT", "ADIA", "AHEL", "AFAV", "AIPL", "REMP", "CRFT",
                                            "LOCK", "INFC", "CURE", "NVPE", "DEAD", "SKIL", "LEVL", "CHBR", "INTM", "BRIB",
                                            "FLAT", "PRFV", "PFIN", "JAIL", "STIJ", "ESIA", "TRES", "ADCR", "ARRT", "CAST"};

// {"cmd": "player_events"}: counts since the last reset.
nlohmann::json Read(Harness&, const nlohmann::json&)
{
    std::scoped_lock _{s_lock};
    // Main thread, no engine locks held: resolve what each event went to.
    std::map<std::tuple<std::string, std::string, bool>, Count> grouped;
    for (const auto& [cKey, cCalls] : s_events)
    {
        auto [target, formId] = Classify(cKey.Handle);
        auto& count = grouped[{cKey.Event, target, cKey.PlayerArgument}];
        count.Calls += cCalls;
        if (count.Examples.size() < 6 && formId)
            count.Examples.insert(formId);
    }
    auto events = nlohmann::json::array();
    for (const auto& [cKey, cCount] : grouped)
        events.push_back({{"event", std::get<0>(cKey)}, {"target", std::get<1>(cKey)}, {"player_argument", std::get<2>(cKey)},
                          {"calls", cCount.Calls}, {"examples", cCount.Examples}});
    auto story = nlohmann::json::object();
    for (const auto& [cIndex, cCalls] : s_storyEvents)
        story[cIndex < std::size(cStoryEventNames) ? cStoryEventNames[cIndex] : std::to_string(cIndex)] = cCalls;
    const double cSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_since).count();
    return {{"ok", true}, {"seconds", cSeconds}, {"sent", s_sent}, {"events", events}, {"story", story},
            {"context_on", s_contextOn}, {"context_hits", s_contextHits}};
}

nlohmann::json Reset(Harness&, const nlohmann::json&)
{
    std::scoped_lock _{s_lock};
    s_events.clear();
    s_storyEvents.clear();
    s_sent = 0;
    s_contextHits = 0;
    s_since = std::chrono::steady_clock::now();
    return Ok();
}

// {"cmd": "player_context", "on": true}: spike 2's GetPlayer context on or off.
nlohmann::json Context(Harness&, const nlohmann::json& acRequest)
{
    s_contextOn = acRequest.value("on", false);
    s_contextCopy = 0;
    s_traceGetPlayer = acRequest.value("trace", false);
    s_traceCause = s_traceGetPlayer;
    std::scoped_lock _{s_tagLock};
    s_pending.clear();
    s_stackTags.clear();
    return Ok();
}

// BGSStoryManager's own log of an event's processing (32374): what each node and quest did with it.
TP_THIS_FUNCTION(TStoryLog, void, void, int32_t, const uint32_t*, TESForm*, TESForm*, bool, float, float);
TStoryLog* RealStoryLog = nullptr;

void TP_MAKE_THISCALL(HookStoryLog, void, int32_t aCode, const uint32_t* apEvent, TESForm* apForm, TESForm* apNode, bool aAte, float aA, float aB)
{
    static constexpr const char* cCodes[] = {"started processing", "timed out", "finished", "node passed", "node failed conditions",
                                            "node at max quests", "quest waiting for reset", "quest already run", "quest started",
                                            "quest failed conditions", "quest failed to fill aliases", "quest timed out filling aliases"};
    if (s_traceStory)
        spdlog::info("Story: event {:X} (type {}) {} {:X}", apEvent ? apEvent[0] : 0, apEvent ? apEvent[1] : 0,
                     aCode >= 0 && aCode < 12 ? cCodes[aCode] : "?", apForm && aCode >= 3 ? apForm->formID : 0);
    TiltedPhoques::ThisCall(RealStoryLog, apThis, aCode, apEvent, apForm, apNode, aAte, aA, aB);
}

TiltedPhoques::Initializer s_storyHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TStoryLog, s_storyLog, 32374);
        RealStoryLog = s_storyLog.Get();
        TP_HOOK(&RealStoryLog, HookStoryLog);
    });

// {"cmd": "story_trace", "on": true}: log the story manager's outcomes and capture the player's CLOC events;
// returns those captured since the last call.
nlohmann::json StoryTrace(Harness&, const nlohmann::json& acRequest)
{
    std::scoped_lock _{s_lock};
    s_traceStory = acRequest.value("on", s_traceStory);
    auto captured = nlohmann::json::array();
    for (const auto& [cOld, cNew] : s_capturedLocationChanges)
        captured.push_back({cOld, cNew});
    s_capturedLocationChanges.clear();
    return {{"ok", true}, {"captured", captured}};
}

// {"cmd": "story_raise_location_change", "actor": id, "old": id, "new": id}: raise a CLOC as the engine does.
nlohmann::json StoryRaise(Harness&, const nlohmann::json& acRequest)
{
    auto* pActor = TESForm::GetById(acRequest.value("actor", 0u));
    if (!pActor)
        return Error("no such actor");
    TP_THIS_FUNCTION(TBuild, LocationChangeEvent*, LocationChangeEvent, TESForm*, TESForm*, TESForm*);
    using TGetManager = void*();
    POINTER_SKYRIMSE(TBuild, s_build, 32288);
    POINTER_SKYRIMSE(TGetManager, s_getManager, 22790);
    LocationChangeEvent event{};
    TiltedPhoques::ThisCall(s_build, &event, pActor, TESForm::GetById(acRequest.value("old", 0u)), TESForm::GetById(acRequest.value("new", 0u)));
    const uint32_t cId = TiltedPhoques::ThisCall(RealAddEvent, s_getManager.Get()(), LocationChangeIndex(), &event);
    return {{"ok", true}, {"event", cId}};
}

// Spike 3: which engine functions that read the player singleton (g_thePlayer, 403521) run, and with a
// remote player's copy among their first four arguments. Count-only stubs, installed on request.
struct ProbeCount
{
    uint64_t Calls;
    uint64_t CopyCalls;
};
constexpr size_t cMaxProbes = 2048;
ProbeCount s_probeCounts[cMaxProbes];
void* s_probeTrampolines[cMaxProbes];
uint32_t s_probeIds[cMaxProbes];
size_t s_probeCount = 0;
void* s_probeCopy = nullptr; // read by the stubs
uint8_t* s_probePage = nullptr;

// lock inc calls; if rcx, rdx, r8 or r9 is the copy, lock inc copy calls; jmp [trampoline]. Only rax and
// r11 change, both volatile and unused for arguments.
void* BuildCountStub(size_t aIndex)
{
    constexpr size_t cStubSize = 80;
    if (!s_probePage)
        s_probePage = static_cast<uint8_t*>(VirtualAlloc(nullptr, cMaxProbes * cStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!s_probePage)
        return nullptr;
    uint8_t* pStub = s_probePage + aIndex * cStubSize;
    size_t at = 0;
    auto emit = [&](std::initializer_list<uint8_t> aBytes) { for (const auto cByte : aBytes) pStub[at++] = cByte; };
    auto emit64 = [&](const void* apValue) { std::memcpy(pStub + at, &apValue, 8); at += 8; };
    emit({0x48, 0xB8}); emit64(&s_probeCounts[aIndex]);  // mov rax, counts
    emit({0xF0, 0x48, 0xFF, 0x00});                       // lock inc qword [rax]
    emit({0x49, 0xBB}); emit64(&s_probeCopy);             // mov r11, &copy
    emit({0x4D, 0x8B, 0x1B});                             // mov r11, [r11]
    emit({0x4D, 0x85, 0xDB, 0x74, 0x19});                 // test r11, r11; jz done
    emit({0x4C, 0x39, 0xD9, 0x74, 0x0F});                 // cmp rcx, r11; je hit
    emit({0x4C, 0x39, 0xDA, 0x74, 0x0A});                 // cmp rdx, r11; je hit
    emit({0x4D, 0x39, 0xD8, 0x74, 0x05});                 // cmp r8, r11; je hit
    emit({0x4D, 0x39, 0xD9, 0x75, 0x05});                 // cmp r9, r11; jne done
    emit({0xF0, 0x48, 0xFF, 0x40, 0x08});                 // hit: lock inc qword [rax+8]
    emit({0x48, 0xB8}); emit64(&s_probeTrampolines[aIndex]); // done: mov rax, &trampoline
    emit({0x48, 0x8B, 0x00, 0xFF, 0xE0});                 // mov rax, [rax]; jmp rax
    FlushInstructionCache(GetCurrentProcess(), pStub, at);
    return pStub;
}

// {"cmd": "player_probe", "ids": [...]}: installs the stubs (once). {"copy": id}: counts reset, the copy set.
nlohmann::json Probe(Harness&, const nlohmann::json& acRequest)
{
    nlohmann::json failed = nlohmann::json::array();
    if (acRequest.contains("ids") && s_probeCount == 0)
    {
        for (const uint32_t cId : acRequest["ids"])
        {
            if (s_probeCount == cMaxProbes)
                break;
            VersionDbPtr<void> function(cId);
            void* pStub = function.GetPtr() ? BuildCountStub(s_probeCount) : nullptr;
            MH_STATUS status = pStub ? MH_CreateHook(function.GetPtr(), pStub, &s_probeTrampolines[s_probeCount]) : MH_UNKNOWN;
            if (status == MH_OK)
                status = MH_QueueEnableHook(function.GetPtr());
            if (status != MH_OK)
            {
                failed.push_back({cId, static_cast<int>(status)});
                continue;
            }
            s_probeIds[s_probeCount++] = cId;
        }
        if (MH_ApplyQueued() != MH_OK)
            return Error("MH_ApplyQueued failed");
    }
    std::memset(s_probeCounts, 0, sizeof(s_probeCounts));
    s_probeCopy = acRequest.contains("copy") ? TESForm::GetById(acRequest["copy"].get<uint32_t>()) : nullptr;
    return {{"ok", true}, {"probes", s_probeCount}, {"failed", failed}, {"copy", s_probeCopy != nullptr}};
}

nlohmann::json ProbeRead(Harness&, const nlohmann::json&)
{
    auto counts = nlohmann::json::object();
    for (size_t i = 0; i < s_probeCount; ++i)
        if (s_probeCounts[i].Calls)
            counts[std::to_string(s_probeIds[i])] = {s_probeCounts[i].Calls, s_probeCounts[i].CopyCalls};
    return {{"ok", true}, {"counts", counts}};
}

constexpr Command cCommands[] = {
    {"player_context", false, &Context},
    {"player_events", false, &Read},
    {"player_events_reset", false, &Reset},
    {"player_probe", false, &Probe},
    {"story_trace", false, &StoryTrace},
    {"story_raise_location_change", true, &StoryRaise},
    {"player_probe_read", false, &ProbeRead},
};
} // namespace
} // namespace TestHarness::PlayerEvents

namespace TestHarness
{
std::span<const Command> PlayerEventsCommands()
{
    return PlayerEvents::cCommands;
}
} // namespace TestHarness
