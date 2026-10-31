#include "AIProcess.h"
#include <Actor.h>
#include <Games/Primitives.h>
#include <VersionDb.h>

void AIProcess::KnockExplosion(Actor* apActor, const NiPoint3* aSourceLocation, float afMagnitude)
{
    using TKnockExplosion = void(AIProcess*, Actor*, const NiPoint3*, float);
    POINTER_SKYRIMSE(TKnockExplosion, knockExplosion, 39895);
    knockExplosion(this, apActor, aSourceLocation, afMagnitude);
}

TESObjectREFR* AIProcess::GetHeadTrackTarget() const noexcept
{
    using TGetHeadTrackTarget = uint32_t*(const AIProcess*, uint32_t*);
    POINTER_SKYRIMSE(TGetHeadTrackTarget, s_getHeadTrackTarget, 39484);
    uint32_t handle = 0;
    return TESObjectREFR::GetByHandle(*s_getHeadTrackTarget(this, &handle));
}

void HighProcessData::SetHeadTrackTarget(HeadTrackType aType, TESObjectREFR* apTarget) noexcept
{
    using TSetHeadTrackTarget = bool(HighProcessData*, uint32_t, TESObjectREFR*);
    POINTER_SKYRIMSE(TSetHeadTrackTarget, s_setHeadTrackTarget, 39783);
    s_setHeadTrackTarget(this, aType, apTarget);
}

std::array<uint32_t, HighProcessData::kHeadTrackTypes> HighProcessData::GetHeadTrackTargets() const noexcept
{
    std::array<uint32_t, kHeadTrackTypes> targets{};
    for (uint32_t i = 0; i < kHeadTrackTypes; ++i)
        if (const TESObjectREFR* pTarget = headTracked[i] ? TESObjectREFR::GetByHandle(headTrackTarget[i]) : nullptr)
            targets[i] = pTarget->formID;
    return targets;
}

void HighProcessData::SetHeadTrackTargets(const std::array<uint32_t, kHeadTrackTypes>& acTargets) noexcept
{
    const auto cCurrent = GetHeadTrackTargets();
    for (uint32_t i = 0; i < kHeadTrackTypes; ++i)
        if (cCurrent[i] != acTargets[i])
            SetHeadTrackTarget(static_cast<HeadTrackType>(i), acTargets[i] ? Cast<TESObjectREFR>(TESForm::GetById(acTargets[i])) : nullptr);
}
