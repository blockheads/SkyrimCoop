// Players on one horse pass through each other (HitRules::RideTogether, docs/re/systems/player-mount.md): a swing's
// pick and sweep, and a projectile's hits, skip the other rider as the engine skips the rider's own mount.
#include <Actor.h>
#include <Combat/HitOwnership.h>
#include <Havok/SwingCast.h>
#include <HookSwitch.h>
#include <Hooks/Initializer.h>
#include <PlayerHands.h>

namespace
{
// Actor::IsInAttackCone (47297): is aTarget within the attack's hit cone. PickSwingTarget (47272) and the sweep
// perk's loop (38603) take only actors it passes; 47272 skips the attacker's own mount itself.
using TInAttackCone = bool(Actor* apAttacker, Actor* apTarget, const float* apFrom, const float* apTo, void* apAttackData,
                           float aConeMult, float* apAngle, bool aIgnoreAngleOffset);
TInAttackCone* RealInAttackCone = nullptr;

bool HookInAttackCone(Actor* apAttacker, Actor* apTarget, const float* apFrom, const float* apTo, void* apAttackData, float aConeMult,
                      float* apAngle, bool aIgnoreAngleOffset)
{
    if (HitRules::RideTogether(apTarget, apAttacker))
        return false;
    return RealInAttackCone(apAttacker, apTarget, apFrom, apTo, apAttackData, aConeMult, apAngle, aIgnoreAngleOffset);
}

// hkpAllCdPointCollector's linear cast (25931), called only by the swing's sweep (38628): its hits on the swinger's
// fellow rider are taken out, so the sweep takes the next body behind it.
using TSwingCast = bool(void* apCollector, void* apWorld, const float* apFrom, const float* apDirection, float aLength);
TSwingCast* RealSwingCast = nullptr;

bool HookSwingCast(void* apCollector, void* apWorld, const float* apFrom, const float* apDirection, float aLength)
{
    const bool cHit = RealSwingCast(apCollector, apWorld, apFrom, apDirection, aLength);
    Actor* pSwinger = PlayerHands::Swinger();
    if (!cHit || !pSwinger)
        return cHit;
    return HitRules::DropFellowRiderHits(apCollector, *pSwinger) > 0;
}
} // namespace

static Hooks::Initializer s_ridersPassThroughHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(47297, &RealInAttackCone, HookInAttackCone);
    HOOK_ID(25931, &RealSwingCast, HookSwingCast);
});
