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

// A machine's key, read from the clone (MachineStates::Key): its name and its states' names.
uint32_t MachineKeyOf(const hkbStateMachineNode& acMachine) noexcept
{
    MachineStates::KeyHash hash;
    hash.Add(HavokName(acMachine.name));
    hash.Add("|");
    for (int32_t i = 0; i < acMachine.states.size; ++i)
    {
        if (i)
            hash.Add(",");
        if (const auto* pState = acMachine.states.data[i]; pState && pState->name)
            hash.Add(HavokName(pState->name));
    }
    return hash.Value;
}

// Its graph rebuilt (a new root behaviour graph): an owner counts a generation and sends a baseline next, a copy forgets
// its owner's states and asks for a baseline.
void NoteGraph(Actor* apActor, const hkbContext* apContext) noexcept
{
    const BShkbAnimationGraph* pGraph = GraphOf(apContext);
    const void* cpRoot = pGraph ? pGraph->behaviorGraph : nullptr;
    auto* pExtension = apActor->GetExtension();
    if (!cpRoot || cpRoot == pExtension->MachineRoot)
        return;
    const bool cRebuilt = pExtension->MachineRoot != nullptr;
    pExtension->MachineRoot = cpRoot;
    if (pExtension->IsCopy())
    {
        pExtension->MachineTimelines.Clear();
        pExtension->NeedsMachineBaseline.store(true, std::memory_order_release);
    }
    else if (cRebuilt)
    {
        pExtension->MachineGeneration.fetch_add(1, std::memory_order_relaxed);
        pExtension->MachineOutbox.Rebuilt();
    }
}

// An owner NPC's machine entered a state (or cInactive): queued for its copies.
void RecordOwnerState(hkbStateMachineNode* apMachine, const hkbContext* apContext, int32_t aState) noexcept
{
    if (!apMachine->name)
        return;
    Actor* pOwner = NpcOf(apContext);
    if (!pOwner || pOwner->GetExtension()->IsCopy())
        return;
    NoteGraph(pOwner, apContext);
    auto* pExtension = pOwner->GetExtension();
    pExtension->MachineOutbox.Push({MachineKeyOf(*apMachine), aState, MachineStates::g_now.load(std::memory_order_relaxed),
                                    pExtension->MachineGeneration.load(std::memory_order_relaxed)});
}
} // namespace

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
// own reselect does (59387: GoToState 59369). A wrong transition still running is cut (force: its transitions cleared,
// a jump); otherwise a transition to the state if one exists, else a jump.
void HookStateMachineUpdate(hkbStateMachineNode* apMachine, const hkbContext* apContext, float aTimestep)
{
    if (apMachine->isActive && apMachine->name)
        if (Actor* pCopy = NpcOf(apContext); pCopy && pCopy->GetExtension()->IsCopy())
        {
            NoteGraph(pCopy, apContext);
            auto& timelines = pCopy->GetExtension()->MachineTimelines;
            if (timelines.Any())
            {
                MachineStates::Machine machine{apMachine->currentStateId, std::nullopt};
                if (const auto& cRunning = apMachine->activeTransitions; cRunning.size > 0 && cRunning.data)
                    machine.Heading = cRunning.data[cRunning.size - 1].toStateId;
                if (const auto cTo = timelines.Reconcile(MachineKeyOf(*apMachine), machine); cTo && HasState(*apMachine, *cTo))
                {
                    using TGoToState = void(hkbStateMachineNode*, int32_t aStateId, const hkbContext*, bool aForce,
                                            bool aJumpIfNoTransition, bool aSetPrevious);
                    POINTER_SKYRIMSE(TGoToState, s_goToState, 59369);
                    COUNT("copy machines reconciled", 1);
                    spdlog::debug("MachineStates: reconcile {:X} {} {} -> {}{} graph {}", pCopy->formID, HavokName(apMachine->name),
                                  machine.Heading.value_or(machine.Current), *cTo, machine.Heading ? " (cut)" : "", timelines.GraphTime());
                    s_goToState(apMachine, *cTo, apContext, machine.Heading.has_value(), true, false);
                }
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
        HOOK_ID(59335, &RealStateMachineUpdate, HookStateMachineUpdate);
        HOOK_ID(59390, &RealSetActiveState, HookSetActiveState);
        HOOK_ID(59337, &RealStateMachineDeactivate, HookStateMachineDeactivate);
    });
