#pragma once

// What SkyrimBridge's files share: its headers and its small lookups.
#include <Camera/PlayerCamera.h>
#include <scripts/PartyNatives.h>
#include <Bridges/SkyrimBridge.h>
#include <items/ContainerItems.h>
#include <core/ItemIds.h>
#include <AI/FollowerTargets.h>
#include <Breadcrumbs.h>
#include <animation/ActorStateRules.h>
#include <HiddenCopies.h>
#include <ReferenceStates.h>
#include <GuestNpcs.h>
#include <core/MainLoopTasks.h>
#include <core/FormIds.h>
#include <characters/NpcSnapshot.h>
#include <IFormFactory.h>
#include <Combat/CombatController.h>
#include <Combat/CombatGroup.h>
#include <Combat/Traps.h>
#include <CopyCombatGroups.h>
#include <Actor.h>
#include <Havok/Ragdoll.h>
#include <Havok/bhkCharacterController.h>
#include <PlayerCharacter.h>
#include <Forms/TESForm.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWeather.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESNPC.h>
#include <ExtraData/ExtraLeveledCreature.h>
#include <Forms/BGSPerk.h>
#include <Memory.h>
#include <Forms/ActorValueInfo.h>
#include <Forms/SpellItem.h>
#include <ActorExtension.h>
#include <core/EngineNames.h>
#include <HitVisualsRules.h>
#include <FormState.h>
#include <items/Overrides.h>
#include <References.h>
#include <TES.h>
#include <Sky/Sky.h>
#include <EquipManager.h>
#include <Interface/UI.h>
#include <Magic/ActorMagicCaster.h>
#include <TimeManager.h>
#include <BSTimer.h>
#include <TESObjectREFR.h>
#include <DefaultObjectManager.h>
#include <Projectiles/Projectile.h>
#include <Forms/TESObjectWEAP.h>
#include <Forms/TESAmmo.h>
#include <Misc/BSFixedString.h>
#include <Forms/TESPackage.h>
#include <Forms/TESTopicInfo.h>
#include <PlayerCollision.h>
#include <RefrInteraction.h>
#include <Forms/TESRace.h>
#include <Forms/BGSEncounterZone.h>
#include <Forms/MagicItem.h>
#include <Magic/MagicTarget.h>
#include <Effects/ActiveEffect.h>
#include <Magic/EffectItem.h>
#include <Bridges/FaceGenSystem.h>
#include <movement/InterpolationSystem.h>
#include <Bridges/AnimationBody.h>
#include <animation/ReplayState.h>
#include <Papyrus/GuestScripts.h>
#include <Misc/SubtitleManager.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>
#include <animation/ActorMediator.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstring>
#include <RTTI.h>
#include <Utils.h>
#include <VersionDb.h>
#include <core/World.h>
#include <core/FormIdComponent.h>
#include <animation/RemoteAnimationComponent.h>

inline Actor* GetActor(uint32_t aFormId) noexcept
{
    return Cast<Actor>(TESForm::GetById(aFormId));
}

inline void ResumeCopyGraph(uint32_t aHandle, uint32_t aAttempt) noexcept
{
    auto* pActor = Cast<Actor>(TESObjectREFR::GetByHandle(aHandle));
    if (!pActor || pActor->IsDead() || !pActor->GetExtension()->IsCopy())
        return;
    pActor->flags2 |= Actor::ActorFlags::SHOULD_ANIM_GRAPH_UPDATE | Actor::ActorFlags::CAN_SPEAK;
    BSFixedString reset("IdleForceDefaultState");
    if (pActor->SendAnimationEvent(&reset))
        spdlog::debug("Copy {:X}: resumed its resurrected graph after {} frames", pActor->formID, aAttempt);
    else if (aAttempt < 120)
        MainLoopTasks::Queue([aHandle, aAttempt] { ResumeCopyGraph(aHandle, aAttempt + 1); });
    else
        spdlog::warn("Copy {:X}: the resurrected graph refused its reset", pActor->formID);
}

inline ActorExtension* GetExtension(uint32_t aFormId) noexcept
{
    Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->GetExtension() : nullptr;
}

inline MagicCaster* GetCaster(Actor* apActor, uint32_t aCastingSource) noexcept
{
    if (!apActor || aCastingSource >= 4)
        return nullptr;
    apActor->GenerateMagicCasters();
    return apActor->GetMagicCaster(static_cast<MagicSystem::CastingSource>(aCastingSource));
}

inline TESObjectREFR* GetRefr(uint32_t aFormId) noexcept
{
    return Cast<TESObjectREFR>(TESForm::GetById(aFormId));
}

inline ExtraLeveledCreature* GetLeveledCreature(Actor* apActor) noexcept
{
    return apActor ? static_cast<ExtraLeveledCreature*>(apActor->extraData.GetByType(ExtraDataType::LeveledCreature)) : nullptr;
}

// The hit shader and art of a magic effect whose visuals last as long as it (FX Persist): those a copy holds. The
// others play once on the owner (ActiveEffectReferenceEffectController::GetEffectPersists).
struct HitVisuals
{
    TESForm* pShader;
    TESForm* pArt;
};

// The effect's hit visuals: those that last as long as it (FX Persist), or those that play once.
inline HitVisuals GetHitVisuals(uint32_t aEffectFormId, bool aPersisting = true) noexcept
{
    const auto* pEffect = Cast<EffectSetting>(TESForm::GetById(aEffectFormId));
    if (!pEffect || !HitVisualsRules::Allowed(pEffect->flags, aPersisting))
        return {};
    return {pEffect->pHitShader, pEffect->pHitArt};
}

// TESObjectREFR::ApplyEffectShader / ApplyArtObject (shader or art, duration (-1: until stopped), facing ref, face
// target, attach to camera, attach node, interface effect), as Papyrus Play and the console's PlayMagicShaderVisuals.
using TApplyVisuals = bool(TESObjectREFR*, TESForm*, float, TESObjectREFR*, bool, bool, void*, bool);

inline void ApplyHitVisuals(Actor* apActor, const HitVisuals& acVisuals, float aDuration) noexcept
{
    POINTER_SKYRIMSE(TApplyVisuals, s_applyEffectShader, 19872);
    POINTER_SKYRIMSE(TApplyVisuals, s_applyArtObject, 22769);
    if (acVisuals.pShader)
        (s_applyEffectShader.Get())(apActor, acVisuals.pShader, aDuration, nullptr, false, false, nullptr, false);
    if (acVisuals.pArt)
        (s_applyArtObject.Get())(apActor, acVisuals.pArt, aDuration, nullptr, false, false, nullptr, false);
}

