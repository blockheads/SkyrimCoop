#include "Combat/CombatController.h"
#include <Actor.h>
#include <TESObjectREFR.h>
#include <RTTI.h>
#include <VersionDb.h>

Actor* CombatController::GetAttacker() const noexcept
{
    if (handleCount)
        return pCachedAttacker.object;
    return Cast<Actor>(TESObjectREFR::GetByHandle(attackerHandle));
}

void CombatController::SetTarget(Actor* apTarget)
{
    using TSetTarget = void(CombatController*, Actor*);
    POINTER_SKYRIMSE(TSetTarget, setTarget, 33235);
    setTarget(this, apTarget);
}


