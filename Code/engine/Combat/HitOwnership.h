#pragma once

#include <Actor.h>
#include <ActorExtension.h>
#include <Havok/SwingCast.h>
#include <combat/HitRules.h>

namespace HitRules
{
inline Party PartyOf(Actor* apActor) noexcept
{
    if (!apActor)
        return Party::None;
    const auto* pExtension = apActor->GetExtension();
    if (pExtension->IsLocalPlayer())
        return Party::LocalPlayer;
    if (pExtension->IsOtherPlayer())
        return Party::OtherPlayer;
    return pExtension->IsLocal() ? Party::Local : Party::Copy;
}

// RideTogether, read from the engine: each rider's own link to its mount.
inline bool RideTogether(Actor* apHittee, Actor* apHitter) noexcept
{
    const auto cMount = [](const Actor* apActor) {
        const TESObjectREFR* pMount = apActor ? apActor->GetRiddenMount() : nullptr;
        return pMount ? pMount->formID : 0u;
    };
    return RideTogether(PartyOf(apHittee), PartyOf(apHitter), cMount(apHittee), cMount(apHitter));
}

// A collector's hits (a swing's sweep, a projectile's step) on another player riding aRider's mount taken out; the rest.
inline int32_t DropFellowRiderHits(void* apCollector, Actor& aRider) noexcept
{
    return GameSwingCast::DropHits(apCollector, [&aRider](const TESObjectREFR& acRef) {
        return RideTogether(Cast<Actor>(const_cast<TESObjectREFR*>(&acRef)), &aRider);
    });
}
} // namespace HitRules
