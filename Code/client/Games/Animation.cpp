#include <HookSwitch.h>
#include <Diagnostics/Counters.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <Forms/TESIdleForm.h>

#include <Structs/ActionEvent.h>

#include <Games/Animation/ActorMediator.h>
#include <Games/Animation/TESActionData.h>

#include <Misc/BSFixedString.h>
#include <Misc/ActorState.h>

#include <Havok/BShkbAnimationGraph.h>
#include <Havok/BSSynchronizedClipGenerator.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <Havok/hkbNode.h>
#include <Havok/bhkCharacterController.h>

#include <World.h>
#include <Systems/AnimationSystem.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Utils.h>
#include <Games/Animation/ReplayCapture.h>
#include <Games/Animation/ReplayState.h>
#include <VersionDb.h>

using TPerformAction = uint8_t(ActorMediator*, TESActionData* apAction);
static TPerformAction* RealPerformAction;
using TUpdateAnimationGraphs = bool(TESObjectREFR*, float aDelta);
static TUpdateAnimationGraphs* RealUpdateAnimationGraphs;
using TUpdateSynchronizedClip = void(BSSynchronizedClipGenerator*, const hkbContext& acContext, float aTimestep);
static TUpdateSynchronizedClip* RealUpdateSynchronizedClip;
using TKillActorEvent = bool(void*, Actor*);
static TKillActorEvent* RealKillActorEvent;
using TContactPoint = void(BSRagdollContactListener*, const hkpContactPointEvent*);
static TContactPoint* RealContactPoint;
using TTryMoveTo = bool(bhkCharacterController*, const void* apPosition, void* apArgument, bool aIgnoreDepth);
static TTryMoveTo* RealTryMoveTo;
using TChooseStartState = int32_t(hkbStateMachineNode*, const hkbContext*);
static TChooseStartState* RealChooseStartState;
using TStateMachineUpdate = void(hkbStateMachineNode*, const hkbContext*, float aTimestep);
static TStateMachineUpdate* RealStateMachineUpdate;
using TSetActiveState = void(hkbStateMachineNode*, int32_t aStateId, bool aSetPrevious, const hkbContext*, bool aSendEvents);
static TSetActiveState* RealSetActiveState;
using TStateMachineDeactivate = void(hkbStateMachineNode*, const hkbContext*);
static TStateMachineDeactivate* RealStateMachineDeactivate;

// TODO: make scoped override
thread_local bool g_forceAnimation = false;
// The local player's own action under way: a paired one (a kill move) starts its partner's half inside it.
thread_local bool s_playerAction = false;
// A replayed action under way: its owner's game judged it.
thread_local bool s_replayedAction = false;

uint8_t HookPerformAction(ActorMediator* apThis, TESActionData* apAction)
{
    auto pActor = apAction->actor;
    const auto pExtension = pActor->GetExtension();
    // A copy's half of the local player's paired action: taken as the player's game plays it, never sent
    // (its owner's game plays it from the player's action).
    const bool cPlayersPartner = s_playerAction && pExtension->IsCopy() && apAction->target == PlayerCharacter::Get();

    if (!pExtension->IsCopy() || g_forceAnimation || cPlayersPartner)
    {
        ActionEvent action;
        action.State1 = pActor->actorState.flags1;
        action.State2 = pActor->actorState.flags2;
        action.Type = apAction->unkInput | (apAction->someFlag ? 0x4 : 0);
        action.Tick = World::Get().GetFrameTick(); // the frame's, as its movement is stamped (S10)
        action.ActorId = pActor->formID;
        action.ActionId = apAction->action->formID;
        action.TargetId = apAction->target ? apAction->target->formID : 0;

        pActor->SaveAnimationVariables(action.Variables);
        action.Variables.GraphRandom = pActor->GetGraphRandom();

        const bool cOuterPlayerAction = pActor == PlayerCharacter::Get() && !s_playerAction;
        s_playerAction |= cOuterPlayerAction;
        const auto cOutcome = ReplayCapture::Run(apAction->someFlag, [&] { return RealPerformAction(apThis, apAction); });
        const auto res = cOutcome.Result;
        if (cOuterPlayerAction)
            s_playerAction = false;

        // spdlog::debug("Action event name: {}, target name: {}", apAction->eventName.AsAscii(), apAction->targetEventName.AsAscii());

        // A resolved idle query selected an animation but never performed it (38949).
        if (!cOutcome.Capture)
            return res;

        action.EventName = apAction->eventName.AsAscii();
        action.TargetEventName = apAction->targetEventName.AsAscii();
        action.IdleId = apAction->idleForm ? apAction->idleForm->formID : 0;

        // An action the engine refused changed nothing here: replayed on a copy it could take
        // (e.g. a late 'MRh_Equipped_Event' after the hands were lowered) and leave the copy
        // showing something its owner never did.
        if (!res)
        {
            if (!g_forceAnimation && !cPlayersPartner)
                spdlog::debug("Owner {:X} action {:X} '{}' refused by the engine, not sent", pActor->formID, action.ActionId, action.EventName);
            return res;
        }

        // A copy whose clip already advanced can recover from the accepted action's result.
        if (!g_forceAnimation && !cPlayersPartner && action.TargetEventName.empty())
        {
            COUNT_TIME("graph state save us");
            pActor->SaveAnimationGraphState(action.GraphState);
            COUNT("graph state bytes", action.GraphState.size());
        }

        if (ReplayState::StartsAttack(action.EventName.c_str()))
            pExtension->ClashUntil = 0;

        // What this actor's graph last took, owner or copy (see AnimationSystem for replays).
        pExtension->LatestAnimation.Write(action);
        if (apAction->action->formID == 0x132AF)
            pExtension->LatestWeapEquipAnimation.Write(action);
        if (!action.TargetEventName.empty())
        {
            pExtension->LatestPairAction.Write(action);
            auto* pPartner = apAction->target && apAction->target->formType == Actor::Type ? static_cast<Actor*>(*apAction->target) : nullptr;
            pExtension->PairedWithCopy = pPartner && pPartner->GetExtension()->IsCopy();
        }

        // Forced by us (a copy converging, a special case of ours) or a copy's paired half: never the owner's own action.
        if (g_forceAnimation || cPlayersPartner)
            return res;

        if (ActorState::LifeStateOf(action.State1) == ActorState::kLifeDying)
            spdlog::debug("Owner {:X} death action {:X} '{}'", pActor->formID, action.ActionId, action.EventName);
        World::Get().GetRunner().Trigger(action);

        return res;
    }

    return 0;
}

// TESObjectREFR::sub_20123 (UpdateAnimationGraphs, our name): an actor's graph update in its animation job
// (RunOneActorAnimationUpdateJob 41453). A copy's replayed action is applied in step with it.
bool HookUpdateAnimationGraphs(TESObjectREFR* apThis, float aDelta)
{
    // Runs for every animated reference each frame: a form type test, not an RTTI cast (copies are never the player).
    if (apThis->formType == Actor::Type && apThis != PlayerCharacter::Get())
    {
        Actor* pActor = static_cast<Actor*>(apThis);
        if (auto* pExtension = pActor->GetExtension(); pExtension->IsCopy())
        {
            AnimationSystem::RunReplayStep(pActor);
            if (ReplayState::PoseHeld(World::Get().GetFrameTick(), pExtension->PoseHeldUntil.load(std::memory_order_relaxed), pActor->IsDead()))
            {
                HoldCopyRagdollStill(*pActor);
                return false;
            }
        }
    }
    return RealUpdateAnimationGraphs(apThis, aDelta);
}

namespace
{
// hkStringPtr: the low bit is a flag.
const char* HavokName(const char* apName) noexcept
{
    return reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(apName) & ~uintptr_t(1));
}

// Its startStateId bound to a graph variable (hkbVariableBindingSet 59467 writes it before activation).
bool StartIsBound(const hkbStateMachineNode& acMachine) noexcept
{
    if (!acMachine.bindings)
        return false;
    for (int32_t i = 0; i < acMachine.bindings->bindings.size; ++i)
    {
        const auto& cBinding = acMachine.bindings->bindings.data[i];
        if (cBinding.bindingType == hkbVariableBindingSet::Binding::kTypeVariable && cBinding.memberPath &&
            std::string_view(HavokName(cBinding.memberPath)) == "startStateId")
            return true;
    }
    return false;
}

bool HasState(const hkbStateMachineNode& acMachine, int32_t aStateId) noexcept
{
    for (int32_t i = 0; i < acMachine.states.size; ++i)
        if (acMachine.states.data[i] && acMachine.states.data[i]->stateId == aStateId)
            return true;
    return false;
}

const BShkbAnimationGraph* GraphOf(const hkbContext* apContext) noexcept
{
    if (!apContext || !apContext->character)
        return nullptr;
    return reinterpret_cast<const BShkbAnimationGraph*>(reinterpret_cast<const uint8_t*>(apContext->character) -
                                                        offsetof(BShkbAnimationGraph, character));
}

// The graph's actor, an NPC's owner or copy (players' copies replay their owner's own graph state).
Actor* NpcOf(const hkbContext* apContext) noexcept
{
    const BShkbAnimationGraph* pGraph = GraphOf(apContext);
    TESObjectREFR* pHolder = pGraph ? pGraph->holder : nullptr;
    if (!pHolder || pHolder->formType != Actor::Type || pHolder == PlayerCharacter::Get())
        return nullptr;
    auto* pActor = static_cast<Actor*>(pHolder);
    return pActor->GetExtension()->IsPlayer() ? nullptr : pActor;
}

// Each machine clone in a behaviour graph and those it references (hkbBehaviorReferenceGenerator), by its behaviour and name.
void WalkMachines(const hkbBehaviorGraphNode* apGraph, std::unordered_map<const void*, uint32_t>& aKeys, std::map<uint32_t, int>& aSeen,
                  int aDepth) noexcept
{
    const hkPointerMapView* pMap = apGraph ? apGraph->nodeTemplateToCloneMap : nullptr;
    if (!pMap || !pMap->elements || aDepth > 8)
        return;
    const std::string_view cBehaviour = apGraph->name ? HavokName(apGraph->name) : "";
    for (int32_t i = 0; i <= pMap->hashMod; ++i)
    {
        const auto& cPair = pMap->elements[i];
        const auto* pNode = cPair.key == ~uintptr_t(0) ? nullptr : reinterpret_cast<const hkbNode*>(cPair.value);
        const hkClass* pClass = pNode ? pNode->GetClassType() : nullptr;
        if (!pClass || !pClass->name)
            continue;
        const std::string_view cClass = pClass->name;
        if (cClass == "hkbBehaviorReferenceGenerator")
            WalkMachines(static_cast<const hkbBehaviorReferenceGenerator*>(pNode)->behavior, aKeys, aSeen, aDepth + 1);
        else if (cClass == "hkbStateMachine" && pNode->name)
        {
            const uint32_t cKey = MachineStates::Key(cBehaviour, HavokName(pNode->name));
            aKeys[pNode] = cKey;
            ++aSeen[cKey];
        }
    }
}

// A machine clone's key (MachineStates::Key), none for a name used twice in its graph. A rebuilt graph starts over: a
// copy forgets its owner's states, an owner counts a generation.
std::optional<uint32_t> MachineKeyOf(Actor* apActor, const hkbContext* apContext, const hkbStateMachineNode* apMachine) noexcept
{
    auto* pExtension = apActor->GetExtension();
    auto& cache = pExtension->MachineKeys;
    const BShkbAnimationGraph* pGraph = GraphOf(apContext);
    const void* cpRoot = pGraph ? pGraph->behaviorGraph : nullptr;
    if (!cpRoot)
        return std::nullopt;
    if (cpRoot != cache.Root)
    {
        if (cache.Root)
        {
            pExtension->MachineGeneration.fetch_add(1, std::memory_order_relaxed);
            if (pExtension->IsCopy())
                pExtension->MachineTimelines.Clear();
        }
        cache = {cpRoot, {}};
    }
    auto it = cache.Keys.find(apMachine);
    if (it == cache.Keys.end())
    {
        std::map<uint32_t, int> seen;
        cache.Keys.clear();
        WalkMachines(reinterpret_cast<const hkbBehaviorGraphNode*>(cpRoot), cache.Keys, seen, 0);
        for (auto& [cClone, key] : cache.Keys)
            if (seen[key] > 1)
                key = 0;
        it = cache.Keys.try_emplace(apMachine, 0).first; // not in its graph's maps: no key, no walk again
    }
    return it->second ? std::optional<uint32_t>(it->second) : std::nullopt;
}

// An owner NPC's tracked machine entered a state (or cInactive): queued for its copies.
void RecordOwnerState(hkbStateMachineNode* apMachine, const hkbContext* apContext, int32_t aState) noexcept
{
    if (!apMachine->name || !MachineStates::Tracked(apMachine->startStateMode, StartIsBound(*apMachine)))
        return;
    Actor* pOwner = NpcOf(apContext);
    if (!pOwner || pOwner->GetExtension()->IsCopy())
        return;
    if (const auto cKey = MachineKeyOf(pOwner, apContext, apMachine))
        pOwner->GetExtension()->MachineOutbox.Push({*cKey, aState, MachineStates::g_now.load(std::memory_order_relaxed)});
}
} // namespace

// hkbStateMachine::ChooseStartStateId (59405), as a machine activates: a copy's tracked machine starts where its owner's
// is at this graph time, when known (MachineStates.h; docs/re/systems/animation-replay-recovery.md).
int32_t HookChooseStartState(hkbStateMachineNode* apMachine, const hkbContext* apContext)
{
    const int32_t cOverride = apMachine->nextStartStateIndexOverride;
    const int32_t cChosen = RealChooseStartState(apMachine, apContext);
    if (cOverride >= 0 || !apMachine->name || !MachineStates::Tracked(apMachine->startStateMode, StartIsBound(*apMachine)))
        return cChosen; // an override is a nested-state transition's: its event reaches the copy too
    Actor* pCopy = NpcOf(apContext);
    if (!pCopy || !pCopy->GetExtension()->IsCopy() || !pCopy->GetExtension()->MachineTimelines.Any())
        return cChosen;
    if (const auto cKey = MachineKeyOf(pCopy, apContext, apMachine))
        if (const auto cWanted = pCopy->GetExtension()->MachineTimelines.Wanted(*cKey); cWanted && HasState(*apMachine, cWanted->State))
            return cWanted->State;
    return cChosen;
}

// hkbStateMachine::SetActiveState (59390): every state a machine enters, from Activate, StartTransition or GoToState.
void HookSetActiveState(hkbStateMachineNode* apMachine, int32_t aStateId, bool aSetPrevious, const hkbContext* apContext, bool aSendEvents)
{
    RealSetActiveState(apMachine, aStateId, aSetPrevious, apContext, aSendEvents);
    RecordOwnerState(apMachine, apContext, aStateId);
}

// hkbStateMachine::Deactivate (59337).
void HookStateMachineDeactivate(hkbStateMachineNode* apMachine, const hkbContext* apContext)
{
    const bool cWasActive = apMachine->isActive;
    RealStateMachineDeactivate(apMachine, apContext);
    if (cWasActive)
        RecordOwnerState(apMachine, apContext, MachineStates::cInactive);
}

// hkbStateMachine::Update (59335): a copy's machine apart from its owner's at this graph time goes there, as the engine's
// own reselect does (59387: GoToState 59369, a transition if one exists, else a jump).
void HookStateMachineUpdate(hkbStateMachineNode* apMachine, const hkbContext* apContext, float aTimestep)
{
    if (apMachine->isActive && apMachine->name)
        if (Actor* pCopy = NpcOf(apContext); pCopy && pCopy->GetExtension()->IsCopy() && pCopy->GetExtension()->MachineTimelines.Any())
            if (const auto cKey = MachineKeyOf(pCopy, apContext, apMachine))
            {
                using TGoToState = void(hkbStateMachineNode*, int32_t aStateId, const hkbContext*, bool aForce, bool aJumpIfNoTransition,
                                        bool aSetPrevious);
                POINTER_SKYRIMSE(TGoToState, s_goToState, 59369);
                const int32_t cFrom = apMachine->currentStateId;
                if (const auto cTo = pCopy->GetExtension()->MachineTimelines.Reconcile(*cKey, cFrom); cTo && HasState(*apMachine, *cTo))
                {
                    COUNT("copy machines reconciled", 1);
                    auto& timelines = pCopy->GetExtension()->MachineTimelines;
                    const auto cWanted = timelines.Wanted(*cKey);
                    spdlog::debug("MachineStates: reconcile {:X} {} {} -> {} wanted-at {} graph {}", pCopy->formID, HavokName(apMachine->name),
                                  cFrom, *cTo, cWanted ? cWanted->At : 0, timelines.GraphTime());
                    s_goToState(apMachine, *cTo, apContext, false, true, false);
                }
            }
    RealStateMachineUpdate(apMachine, apContext, aTimestep);
}

// A copy's half of a paired animation is at its mark: its owner's game placed it, and the engine cannot walk
// a copy there, so a kill move with a copy waited out the engine's fallback (docs/re/systems/actor-ai-and-copies.md).
void HookUpdateSynchronizedClip(BSSynchronizedClipGenerator* apThis, const hkbContext& acContext, float aTimestep)
{
    RealUpdateSynchronizedClip(apThis, acContext, aTimestep);
    if (apThis->atMark || !acContext.character)
        return;
    const auto* pGraph = reinterpret_cast<const BShkbAnimationGraph*>(reinterpret_cast<const uint8_t*>(acContext.character) -
                                                                      offsetof(BShkbAnimationGraph, character));
    if (pGraph->holder && pGraph->holder->GetExtension()->IsCopy())
        apThis->atMark = true;
}

// bhkCharacterController::TryMoveTo 78260, the sweep a pair starts with (32792): each game judges it on its own positions,
// so a replayed pair could be refused after its owner's game started it (docs/re/systems/kill-moves.md).
bool HookTryMoveTo(bhkCharacterController* apThis, const void* apPosition, void* apArgument, bool aIgnoreDepth)
{
    return ReplayState::PairMarkClear(RealTryMoveTo(apThis, apPosition, apArgument, aIgnoreDepth), s_replayedAction);
}

// The victim state's contact listener (63092): any penetrating contact kills a kill move's victim on the spot. Across
// games the attacker's game judged the pair, and the victim's body stands elsewhere: its clip's KillActor kills it.
void HookContactPoint(BSRagdollContactListener* apThis, const hkpContactPointEvent* apEvent)
{
    const bool cTriggered = apThis->triggered;
    RealContactPoint(apThis, apEvent);
    if (cTriggered || !apThis->triggered)
        return;
    for (const hkpRigidBody* pBody : apEvent->bodies)
    {
        TESObjectREFR* pRef = pBody ? ReferenceOf(*pBody) : nullptr;
        const auto* pExtension = pRef && pRef->formType == Actor::Type ? static_cast<Actor*>(pRef)->GetExtension() : nullptr;
        if (pExtension && !ReplayState::ContactKills(pExtension->PairedWithCopy || pExtension->IsCopy()))
        {
            spdlog::debug("{:X}: a kill move's contact kill held back, a copy is in the pair", pRef->formID);
            apThis->triggered = false;
            return;
        }
    }
}

// KillActorHandler::Handle 42838, on a graph's KillActor event: a copy holds its pose until its owner's death action.
bool HookKillActorEvent(void* apThis, Actor* apActor)
{
    if (auto* pExtension = apActor->GetExtension(); pExtension->IsCopy())
        pExtension->PoseHeldUntil.store(World::Get().GetFrameTick() + ReplayState::cPoseHoldMs, std::memory_order_relaxed);
    return RealKillActorEvent(apThis, apActor);
}

ActorMediator* ActorMediator::Get() noexcept
{
    POINTER_SKYRIMSE(ActorMediator*, s_actorMediator, 403567);

    return *(s_actorMediator.Get());
}

bool ActorMediator::PerformAction(TESActionData* apAction) noexcept
{
    if (apAction->actor->formID == 0x13482)
    {
        /*static std::unordered_set<uint32_t> s_ids;

        spdlog::error("New frame");
        for(auto i = 0; i < action.Variables.size(); ++i)
        {
            auto& oldVars = pExtension->LatestVariables.Variables;
            auto& newVars = action.Variables;
            if(oldVars[i] != newVars[i] && s_ids.count(i) == 0)
            {
                //s_ids.insert(i);
                spdlog::info("Var {} changed from {} to {}", i, oldVars[i], newVars[i]);
            }
        }*/
        // spdlog::info("Play animation name: {} with idle {:X} and target {:X} and unk {:X}", apAction->action->keyword.AsAscii(), (apAction->idleForm ? apAction->idleForm->formID : 0), (apAction->target ? apAction->target->formID : 0), apAction->unkInput);
    }

    const auto res = RealPerformAction(this, apAction);
    // const auto res = RePerformAction(apAction, aValue);

    if (res && apAction->actor->formID == 0x13482)
    {
        //    spdlog::info("Passed !");
    }

    return res != 0;
}

bool ActorMediator::ForceAction(TESActionData* apAction) noexcept
{
    using TAnimationStep = uint8_t(ActorMediator*, TESActionData*);

    POINTER_SKYRIMSE(TAnimationStep, PerformComplexAction, 38953);

    uint8_t result = 0;

    auto pActor = static_cast<Actor*>(apAction->actor);
    if (!pActor || EngineVirtual(pActor->animationGraphHolder).IsReady())
    {
        s_replayedAction = true;
        result = PerformComplexAction(this, apAction);
        s_replayedAction = false;
    }

    return result;
}

ActionInput::ActionInput(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
{
    // skip vtable as we never use this directly
    actor = apActor;
    target = apTarget;
    action = apAction;
    unkInput = aParam1;
}

void ActionInput::Release()
{
    actor.Release();
    target.Release();
}

ActionOutput::ActionOutput()
    : eventName("")
    , targetEventName("")
{
    // skip vtable as we never use this directly

    result = 0;
    targetIdleForm = nullptr;
    idleForm = nullptr;
    unk1 = 0;
}

void ActionOutput::Release()
{
    eventName.Release();
    targetEventName.Release();
}

BGSActionData::BGSActionData(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
    : ActionInput(aParam1, apActor, apAction, apTarget)
{
    // skip vtable as we never use this directly
    someFlag = 0;
}

TESActionData::TESActionData(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
    : BGSActionData(aParam1, apActor, apAction, apTarget)
{
    POINTER_SKYRIMSE(void*, s_vtbl, 188603);

    someFlag = false;

    *reinterpret_cast<void**>(this) = s_vtbl.Get();
}

TESActionData::~TESActionData()
{
    ActionOutput::Release();
    ActionInput::Release();
}

static Hooks::Initializer s_animationHook(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(38949, &RealPerformAction, HookPerformAction);
        HOOK_ID(20123, &RealUpdateAnimationGraphs, HookUpdateAnimationGraphs);
        HOOK_ID(63050, &RealUpdateSynchronizedClip, HookUpdateSynchronizedClip); // BSSynchronizedClipGenerator::Update (vtable 5)
        HOOK_ID(63092, &RealContactPoint, HookContactPoint);
        HOOK_ID(42838, &RealKillActorEvent, HookKillActorEvent);
        HOOK_ID(78260, &RealTryMoveTo, HookTryMoveTo);
        HOOK_ID(59405, &RealChooseStartState, HookChooseStartState);
        HOOK_ID(59335, &RealStateMachineUpdate, HookStateMachineUpdate);
        HOOK_ID(59390, &RealSetActiveState, HookSetActiveState);
        HOOK_ID(59337, &RealStateMachineDeactivate, HookStateMachineDeactivate);
    });
