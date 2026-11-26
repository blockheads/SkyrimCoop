// Actor's detection hooks: what a witness knows, the noise an actor makes.
#include <CopyCombatGroups.h>
#include <Forms/BGSAction.h>
#include <Interface/ItemMenuWatch.h>
#include <animation/ActorMediator.h>
#include <Animation/TESActionData.h>
#include <animation/ReplayState.h>
#include <animation/ActorStateRules.h>
#include <animation/GraphReference.h>
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

#include <BSAnimationGraphManager.h>

#include <Hooks/Initializer.h>

// ActorKnowledge::SetDetectionState (42704), the detection job's pass: a copy sees our player unseen (its eye is the host's).
using TUpdateDetectionState = void(ActorKnowledge*, CrimeEngine::DetectionState*);

static TUpdateDetectionState* RealUpdateDetectionState = nullptr;

void HookUpdateDetectionState(ActorKnowledge* apThis, CrimeEngine::DetectionState* apState)
{
    auto* pWitness = Cast<Actor>(TESObjectREFR::GetByHandle(apThis->hOwner));
    auto* pTarget = Cast<Actor>(TESObjectREFR::GetByHandle(apThis->hTarget));
    if (apState && pWitness && pTarget)
    {
        const auto cPass = DetectionRules::Installed(pWitness->GetExtension()->IsCopy(), pTarget->GetExtension()->IsLocalPlayer(),
                                                     {apState->Level, apState->LineOfSight, apState->Seen, apState->Unk16});
        apState->Level = cPass.Level;
        apState->LineOfSight = cPass.LineOfSight;
        apState->Seen = cPass.Seen;
        apState->Unk16 = cPass.Unk16;
    }
    RealUpdateDetectionState(apThis, apState);
}

// Actor::SetActionValue (37711): the noise an actor's action makes (a swing, a cast, the player's bump). A copy's is its
// owner's, from its record (ActorStateService), not its own engine's.
using TSetActionValue = void(Actor*, int32_t);

static TSetActionValue* RealSetActionValue = nullptr;

void HookSetActionValue(Actor* apThis, int32_t aNoise)
{
    if (!ActorStateRules::MakesOwnNoise(apThis->GetExtension()->IsCopy()))
        return;
    RealSetActionValue(apThis, aNoise);
}

static Hooks::Initializer s_actorDetectionHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(42704, &RealUpdateDetectionState, HookUpdateDetectionState);
        HOOK_ID(37711, &RealSetActionValue, HookSetActionValue);
    });
