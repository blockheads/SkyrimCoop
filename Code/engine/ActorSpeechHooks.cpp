// Actor's speech hooks: voice lines and dialogue responses.
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
#include <Havok/hkbStateMachine.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Forms/BGSOutfit.h>
#include <ScriptEquips.h>
#include <Forms/TESObjectARMO.h>

#include <ModCompat/BehaviorVar.h>

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <Utils.h>
#include <VersionDb.h>
#include <core/FormIds.h>

struct DialogueItem;

// TODO: This is an AIProcess function
// The engine passes a fifth argument (a byte it hands on to the response's processing); dropping it left
// the original reading an uninitialized stack slot.
using TProcessResponse = uint64_t(void*, DialogueItem* apVoice, Actor* apTalkingActor, Actor* apTalkedToActor, bool aUnk);

static TProcessResponse* RealProcessResponse = nullptr;

uint64_t HookProcessResponse(void* apThis, DialogueItem* apVoice, Actor* apTalkingActor, Actor* apTalkedToActor, bool aUnk)
{
    if (apTalkingActor)
    {
        if (apTalkingActor->GetExtension()->IsOtherPlayer())
            return 0;
    }
    return RealProcessResponse(apThis, apVoice, apTalkingActor, apTalkedToActor, aUnk);
}

// Character::SpeakSound (37542): queues a line; returns when it ends (game seconds), -1 without a voice, which
// Actor::UpdateInDialogue (37541) takes as said: the menu's voice is ready and it takes a pick.
using TSpeakSoundFunction = float(Actor*, const char* apName, uint32_t* a3, uint32_t a4, uint32_t a5, uint32_t a6, uint64_t a7, uint64_t a8, uint64_t a9, bool a10, uint64_t a11, bool a12, bool a13, bool a14);

static TSpeakSoundFunction* RealSpeakSoundFunction = nullptr;

float HookSpeakSoundFunction(Actor* apThis, const char* apName, uint32_t* a3, uint32_t a4, uint32_t a5, uint32_t a6, uint64_t a7, uint64_t a8, uint64_t a9, bool a10, uint64_t a11, bool a12, bool a13, bool a14)
{
    spdlog::debug("Voice: {:X} ({}) says {}", apThis->formID,
                  apThis->GetExtension()->IsCopy() ? "copy, refused" : apThis->GetExtension()->IsLocal() ? "local" : "not ours", apName ? apName : "");

    if (apThis->GetExtension()->IsLocal())
        World::Get().GetRunner().Trigger(DialogueEvent(apThis->formID, apName));
    // A copy says only its owner's lines (NotifyDialogue calls past this hook), none its game picks: no voice.
    else if (apThis->GetExtension()->IsCopy())
        return -1.f;

    return RealSpeakSoundFunction(apThis, apName, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
}

// Actor::UpdateInDialogue (37541, vfunc 0x4C): SpeakerUpdate.h says why. DialogueBridge::ClearOrphaned ends it.
using TUpdateInDialogue = bool(Actor*, void* apResponse, bool aUnused);

static TUpdateInDialogue* RealUpdateInDialogue = nullptr;

bool HookUpdateInDialogue(Actor* apThis, void* apResponse, bool aUnused)
{
    if (SpeakerUpdate::Skipped(apThis->currentProcess != nullptr, World::Get().GetTransport().IsOnline()))
        return false;
    return RealUpdateInDialogue(apThis, apResponse, aUnused);
}

void Actor::SpeakSound(const char* pFile)
{
    uint32_t handle[3]{};
    handle[0] = -1;
    RealSpeakSoundFunction(this, pFile, handle, 0, 0x32, 0, 0, 0, 0, 0, 0, 0, 1, 1);
}

static Hooks::Initializer s_actorSpeechHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(39643, &RealProcessResponse, HookProcessResponse);
        HOOK_ID(37542, &RealSpeakSoundFunction, HookSpeakSoundFunction);
        HOOK_ID(37541, &RealUpdateInDialogue, HookUpdateInDialogue);
    });
