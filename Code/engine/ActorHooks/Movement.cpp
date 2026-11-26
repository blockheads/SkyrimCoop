// Actor's movement hooks: placing, stepping a copy, a copy's AI upkeep.
#include <CopyCombatGroups.h>
#include <Forms/BGSAction.h>
#include <Interface/ItemMenuWatch.h>
#include <animation/ActorMediator.h>
#include <Animation/TESActionData.h>
#include <animation/ReplayState.h>
#include <animation/ActorStateRules.h>
#include <animation/GraphReference.h>
#include <BSAnimationGraphManager.h>
#include <CrashHandler.h>
#include <HookSwitch.h>
#include <BSCore/LockOrder.h>
#include <core/EngineNames.h>
#include <References.h>
#include <characters/ActorPools.h>
#include <EquipManager.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>
#include <Havok/bhkCharacterController.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <movement/CopyStep.h>
#include <Combat/WorldHits.h>
#include <Misc/GameVM.h>
#include <DefaultObjectManager.h>
#include <Forms/TESNPC.h>
#include <Forms/TESFaction.h>
#include <Components/TESActorBaseData.h>
#include <ExtraData/ExtraFactionChanges.h>
#include <Memory.h>
#include <RefrInteraction.h>
#include <Forms/TESLevItem.h>
#include <Combat/CombatController.h>

#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <Papyrus/GuestScripts.h>
#include <items/InventoryChangeEvent.h>
#include <items/WornResetEvent.h>
#include <items/LooseItemEvents.h>
#include <mounts/MountEvent.h>
#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>
#include <dialogue/DialogueEvent.h>
#include <combat/HitEvent.h>
#include <Combat/HitOwnership.h>
#include <Combat/PartyAggro.h>
#include <magic/RemoveSpellEvent.h>

#include <TES.h>
#include <GuestNpcs.h>
#include <characters/NpcHeldEvent.h>
#include <core/World.h>
#include <ports/IActorBridge.h>
#include <world/ActivatePrompts.h>
#include <dialogue/SpeakerUpdate.h>
#include <Bridges/PapyrusService.h>
#include <death/DeathDrops.h>

#include <Forms/ActorValueInfo.h>
#include <Forms/TESRace.h>

#include <Effects/ValueModifierEffect.h>

#include <Misc/InventoryEntry.h>
#include <ExtraData/ExtraCount.h>
#include <Misc/ActorKnowledge.h>
#include <Crime.h>
#include <crime/DetectionRules.h>

#include <ExtraData/ExtraDataList.h>
#include <ExtraData/ExtraInteraction.h>
#include <ExtraData/ExtraCharge.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraEnchantment.h>
#include <ExtraData/ExtraHealth.h>
#include <ExtraData/ExtraPoison.h>
#include <ExtraData/ExtraSoul.h>
#include <ExtraData/ExtraTextDisplayData.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/AlchemyItem.h>
#include <core/ItemIds.h>
#include <Forms/TESObjectCELL.h>

#include <Structs/Skyrim/AnimationGraphDescriptor_BHR_Master.h>

#include <items/Overrides.h>
#include <BSAnimationGraphManager.h>

#include <Hooks/Initializer.h>

using TSetPosition = char(Actor*, NiPoint3& acPosition);

using TApplyMovement = void(Actor*, float aDelta);

static TSetPosition* RealSetPosition = nullptr;

static TApplyMovement* RealApplyMovement = nullptr;

char HookSetPosition(Actor* apThis, NiPoint3& aPosition)
{
    const auto pExtension = apThis ? apThis->GetExtension() : nullptr;
    const auto bIsCopy = pExtension && pExtension->IsCopy();

    // A copy is placed by the sync, except while its engine steps it (its controller's place written back after
    // the physics step: HookApplyMovement) or its seat carries it (its REFRSyncController, 20318).
    if (bIsCopy && !ScopedReferencesOverride::IsOverriden() &&
        !ReplayState::CopyTakesEnginePlace(pExtension->HasCopyGoal, apThis->extraData.Contains(ExtraDataType::Interaction),
                                           apThis->actorState.GetSitSleepState()))
        return 1;

    // A non-actor's move reaches the change funnel (RecordMove).
    if (apThis->formType != Actor::Type)
    {
        const NiPoint3 cBefore = apThis->position;
        const char cResult = RealSetPosition(apThis, aPosition);
        apThis->RecordMove(apThis->position != cBefore);
        return cResult;
    }

    // Don't interfere with the player, or if we are calling our self
    if (apThis == PlayerCharacter::Get() || ScopedReferencesOverride::IsOverriden())
        return RealSetPosition(apThis, aPosition);

    ScopedReferencesOverride recursionGuard;

    // A stepped copy's controller's place, at its owner's height on its owner's place (CopyStep::RestHeight). Writes the
    // reference only (no warp): its capsule rests where it does.
    NiPoint3 place = aPosition;
    if (bIsCopy && pExtension->HasCopyGoal)
        place.z = CopyStep::RestHeight({place.x, place.y, place.z}, {pExtension->CopyGoal[0], pExtension->CopyGoal[1], pExtension->CopyGoal[2]});

    // It just works TM
    apThis->SetPosition(place, false);

    return 1;
}

// Actor's BSTEventSink<BSTransformDeltaEvent>::ProcessEvent (37996): its graph's root motion, and an actor in its
// ragdoll kept on it (its controller, SetRotationZ, SetPosition). A copy lying in its ragdoll is let through, as its
// owner is: moved anywhere else in the frame, its skeleton shows its old pose at the new place (physics-sync.md).
using TTransformDelta = uint32_t(void*, const void*, void*);

static TTransformDelta* RealTransformDelta = nullptr;

uint32_t HookTransformDelta(void* apSink, const void* apEvent, void* apSource)
{
    auto* pActor = reinterpret_cast<Actor*>(static_cast<uint8_t*>(apSink) - offsetof(Actor, tdEvent));
    if (const auto* pExtension = pActor->GetExtension(); pExtension && pExtension->IsCopy() && pActor->LiesInRagdoll())
    {
        ScopedReferencesOverride engineMoves;
        return RealTransformDelta(apSink, apEvent, apSource);
    }
    return RealTransformDelta(apSink, apEvent, apSource);
}

using TForceState = void(Actor*, const NiPoint3&, float, float, TESObjectCELL*, TESWorldSpace*, bool);

static TForceState* RealForceState = nullptr;

void HookForceState(Actor* apThis, const NiPoint3& acPosition, float aX, float aZ, TESObjectCELL* apCell, TESWorldSpace* apWorldSpace, bool aUnkBool)
{
    /*const auto pNpc = Cast<TESNPC>(apThis->baseForm);
    if (pNpc)
    {
        spdlog::info("For TESNPC: {}, spawn at {} {} {}", pNpc->fullName.value, apPosition->m_x, apPosition->m_y,
                     apPosition->m_z);
    }*/

    // if (apThis != PlayerCharacter::Get())
    //     return;

    return RealForceState(apThis, acPosition, aX, aZ, apCell, apWorldSpace, aUnkBool);
}

// Actor::ApplyMovement (37356): a copy runs none of its decisions, only its engine's step onto its owner's place
// (Actor::Move, so its capsule collides and pushes: docs/re/systems/actor-movement.md).
// Out of line: a lock order its Move starts stays ours on the checker's stacks (LockOrder::IgnoreFrames below).
__declspec(noinline) static void StepCopy(Actor* apThis, ActorExtension* apExtension, float aDelta)
{
    if (!apExtension->HasCopyGoal || aDelta <= 0.f)
        return;
    const NiPoint3 cGoal(glm::vec3(apExtension->CopyGoal[0], apExtension->CopyGoal[1], apExtension->CopyGoal[2]));
    auto* pProcess = apThis->currentProcess;
    const bool cCanStep = pProcess && pProcess->middleProcess && pProcess->middleProcess->charController &&
                          apThis->parentCell && apThis->parentCell->IsFullyLoaded();
    if (!cCanStep)
    {
        apThis->ForcePosition(cGoal);
        return;
    }
    const glm::vec3 cLocal = CopyStep::Translation({cGoal.x - apThis->position.x, cGoal.y - apThis->position.y,
                                                    cGoal.z - apThis->position.z}, apThis->rotation.z);
    apThis->Move(aDelta, NiPoint3(cLocal));
}

void HookApplyMovement(Actor* apThis, float aDelta)
{
    auto* pExtension = apThis->GetExtension();
    if (pExtension->IsCopy())
        return StepCopy(apThis, pExtension, aDelta);
    RealApplyMovement(apThis, aDelta);
}

// Character::UpdateAI (37347). Copies skip it but run its body upkeep below
// (docs/re/systems/actor-ai-and-copies.md).
using TUpdateAI = void(Actor*, float aDelta);

static TUpdateAI* RealUpdateAI = nullptr;

// Character::UpdateCellAttachment (37352): loads/unloads 3D as the actor's cell attaches.
using TUpdateCellAttachment = void(Actor*);

static TUpdateCellAttachment* UpdateCellAttachment = nullptr;

// Character::UpdateVoiceTimer (37752): counts the voice line down and marks it ended.
using TUpdateVoiceTimer = void(Actor*, bool aForceEnd);

static TUpdateVoiceTimer* UpdateVoiceTimer = nullptr;

// Character::CalculateEssentialProtected (37227): kEssential/kProtected.
using TCalculateEssentialProtected = void(Actor*);

static TCalculateEssentialProtected* CalculateEssentialProtected = nullptr;

// AIProcess::GetLipData (39373), AIProcess::UpdateLipSyncGraph (39377): mouth movement of races
// without a FaceGen head (dragons).
using TGetLipData = void*(AIProcess*);

static TGetLipData* GetLipData = nullptr;

using TUpdateLipSyncGraph = void(AIProcess*, void* apLipData, Actor* apActor);

static TUpdateLipSyncGraph* UpdateLipSyncGraph = nullptr;

// A copy's part of UpdateAI: its body's upkeep only. Not inlined: HookUpdateAI's own frames are a pass-through.
__declspec(noinline) void UpdateCopyAI(Actor* apThis)
{
    // UpdateAI's own early return.
    if (!(apThis->flags1 & Actor::kProcessMe) || apThis->IsDisabled())
        return;

    UpdateVoiceTimer(apThis, false);
    apThis->UpdateSoundCallback(false);

    // Copies die with their owner, never from their own health (as skymp keeps its remote actors).
    if (!apThis->IsDead())
        apThis->StartDeferredKill();
    else if (ActorState::LifeStateOf(apThis->actorState.flags1) == ActorState::kLifeDying &&
             apThis->LiesInRagdoll() && !apThis->IsInKillMove())
    {
        // Copies skip native death upkeep; a ragdoll must leave kDying before NPC activation permits looting.
        POINTER_SKYRIMSE(void(Actor*, uint32_t), s_setLifeState, 37612);
        s_setLifeState(apThis, ActorState::kLifeDead);
    }

    AIProcess* pProcess = apThis->currentProcess;
    if (pProcess && pProcess->processLevel == 0)
    {
        CalculateEssentialProtected(apThis);
        if (void* pLipData = GetLipData(pProcess); pLipData && apThis->race && !(apThis->race->raceFlags & TESRace::kFaceGenHead))
            UpdateLipSyncGraph(pProcess, pLipData, apThis);
    }

    // UpdateAI does this last, below high process.
    if (apThis->baseForm && (!pProcess || pProcess->processLevel != 0))
        UpdateCellAttachment(apThis);
}

void HookUpdateAI(Actor* apThis, float aDelta)
{
    if (apThis->GetExtension()->IsCopy())
        return UpdateCopyAI(apThis);
    // A guest's own NPC stands still until the host spawns its copy (GuestNpcs).
    if (apThis->IsHeldOnGuest())
        return;
    RealUpdateAI(apThis, aDelta);
}

static Hooks::Initializer s_actorMovementHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TUpdateCellAttachment, s_updateCellAttachment, 37352);
        UpdateCellAttachment = s_updateCellAttachment.Get();
        POINTER_SKYRIMSE(TUpdateVoiceTimer, s_updateVoiceTimer, 37752);
        UpdateVoiceTimer = s_updateVoiceTimer.Get();
        POINTER_SKYRIMSE(TCalculateEssentialProtected, s_calculateEssentialProtected, 37227);
        CalculateEssentialProtected = s_calculateEssentialProtected.Get();
        POINTER_SKYRIMSE(TGetLipData, s_getLipData, 39373);
        GetLipData = s_getLipData.Get();
        POINTER_SKYRIMSE(TUpdateLipSyncGraph, s_updateLipSyncGraph, 39377);
        UpdateLipSyncGraph = s_updateLipSyncGraph.Get();
        HOOK_ID(37356, &RealApplyMovement, HookApplyMovement);
        HOOK_ID(37347, &RealUpdateAI, HookUpdateAI);
        // Every actor's AI runs through it: a lock order there is the engine's unless a copy's runs (UpdateCopyAI).
        LockOrder::IgnoreFrames(reinterpret_cast<const void*>(&HookUpdateAI));
        // Every actor's movement step (pathing locks): the engine's; a copy's step is StepCopy.
        LockOrder::IgnoreFrames(reinterpret_cast<const void*>(&HookApplyMovement));
        HOOK_ID(19790, &RealSetPosition, HookSetPosition);
        HOOK_ID(37996, &RealTransformDelta, HookTransformDelta);
        HOOK_ID(37313, &RealForceState, HookForceState);
    });
