// Actor's mount hooks: getting on, riding along, getting off.
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
#include <ExtraData/ExtraHealth.h>

#include <BSAnimationGraphManager.h>

#include <Hooks/Initializer.h>

using TInitiateMountPackage = bool(Actor*, Actor* apMount);

static TInitiateMountPackage* RealInitiateMountPackage = nullptr;

using TDetachInteraction = uint64_t(RefrInteraction*, bool);

static TDetachInteraction* RealDetachInteraction = nullptr;

bool Actor::InitiateMountPackage(Actor* apMount) noexcept
{
    return RealInitiateMountPackage(this, apMount);
}

// A ride started (37905): true for the mount package; false for a refusal (CanRide 38706) and for the low-process
// at-once attach (40345, 41532), which the rider's ExtraInteraction shows. A refused try reserves no horse.
bool HookInitiateMountPackage(Actor* apThis, Actor* apMount)
{
    const bool cStarted = RealInitiateMountPackage(apThis, apMount);
    if (apMount && apThis->GetExtension()->IsLocal() && (cStarted || apThis->extraData.Contains(ExtraDataType::Interaction)))
        World::Runner().Trigger(MountEvent(apThis->formID, apMount->formID));
    return cStarted;
}

// Every ride ends here (RefrInteraction::Detach 41539): the dismount clip's end, StopRiding, jail, a fine, a load.
uint64_t HookDetachInteraction(RefrInteraction* apThis, bool aUnk)
{
    auto* pRider = apThis ? Cast<Actor>(TESObjectREFR::GetByHandle(apThis->actorHandle)) : nullptr;
    const auto* cpMount = apThis ? TESObjectREFR::GetByHandle(apThis->targetHandle) : nullptr;
    const uint32_t cMountId = cpMount ? cpMount->formID : 0;
    const uint64_t cResult = RealDetachInteraction(apThis, aUnk);
    if (pRider)
        World::Runner().Trigger(DismountEvent{pRider->formID, cMountId, !pRider->GetExtension()->IsLocal()});
    return cResult;
}

using TActivateMount = void(Actor*, TESObjectREFR* apTarget, int32_t, int32_t, uint8_t);

static TActivateMount* RealActivateMount = nullptr;

// A horse a party player drives: our player may board it (CharacterService::OnBoardEvent).
static bool OffersBoarding(Actor* apMount) noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || !apMount || !apMount->IsMount() || apMount->IsDead() || pPlayer->extraData.Contains(ExtraDataType::Interaction))
        return false;
    const RefrInteraction* pLink = apMount->GetOwnInteraction();
    auto* pRider = pLink ? Cast<Actor>(TESObjectREFR::GetByHandle(pLink->actorHandle)) : nullptr;
    return pRider && pRider != pPlayer && pRider->GetExtension()->IsOtherPlayer();
}

using TGetActivateText = bool(TESNPC*, TESObjectREFR* apTarget, BSString* apText);

static TGetActivateText* RealGetActivateText = nullptr;

// The crosshair's prompt (TESNPC::GetActivateText 24716; false: none, UpdateCrosshairs 40621): none over another
// player's character. A horse offers Ride only when CanRide (38706) passes, which refuses a ridden one; a horse a party
// player drives offers it too.
bool HookGetActivateText(TESNPC* apThis, TESObjectREFR* apTarget, BSString* apText)
{
    if (auto* pActor = Cast<Actor>(apTarget); pActor && !ActivatePrompts::Offered(pActor->GetExtension()->IsOtherPlayer()))
        return false;
    if (RealGetActivateText(apThis, apTarget, apText))
        return true;
    auto* pMount = Cast<Actor>(apTarget);
    if (!apText || !OffersBoarding(pMount))
        return false;
    POINTER_SKYRIMSE(Setting, s_ride, 377833); // gGameSetting_sRide
    const char* cpRide = reinterpret_cast<const char*>(s_ride.Get()->data); // a string setting's text
    const std::string cText = std::format("{}\n{}", cpRide ? cpRide : "Ride", pMount->GetDisplayFullName());
    apText->Set(cText.c_str());
    return true;
}

// A character activating a mount (37864): ridden by another, nothing; our player's may board it as a passenger.
void HookActivateMount(Actor* apThis, TESObjectREFR* apTarget, int32_t aUnk1, int32_t aUnk2, uint8_t aUnk3)
{
    RealActivateMount(apThis, apTarget, aUnk1, aUnk2, aUnk3);
    auto* pMount = Cast<Actor>(apTarget);
    if (!apThis || apThis != PlayerCharacter::Get() || !pMount || !pMount->IsMount())
        return;
    if (OffersBoarding(pMount))
        return World::Runner().Trigger(BoardEvent{apThis->formID, pMount->formID});
    const RefrInteraction* pLink = pMount->GetOwnInteraction();
    spdlog::info("Activated mount {:X}: no boarding (our player riding {}, its slot {}, its rider {:X})", pMount->formID,
                 apThis->extraData.Contains(ExtraDataType::Interaction), pLink != nullptr,
                 pLink ? pLink->actorHandle : 0u);
}

static Hooks::Initializer s_actorMountHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(37905, &RealInitiateMountPackage, HookInitiateMountPackage);
        HOOK_ID(41539, &RealDetachInteraction, HookDetachInteraction);
        HOOK_ID(37864, &RealActivateMount, HookActivateMount);
        HOOK_ID(24716, &RealGetActivateText, HookGetActivateText);
    });
