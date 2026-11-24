#pragma once

#include <Bridges/IFormBridge.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <FormIds.h>
#include <Messages/Payloads.h>

// A launch on the wire (its shooter aside) and made again here: an actor's (CombatService) and an object's (HostTraps, GuestTraps).
namespace ProjectileLaunches
{
inline void Fill(Payloads::ProjectileLaunch& aLaunch, const ProjectileLaunchedEvent& acEvent) noexcept
{
    aLaunch.OriginX = acEvent.Origin.x;
    aLaunch.OriginY = acEvent.Origin.y;
    aLaunch.OriginZ = acEvent.Origin.z;
    aLaunch.ProjectileBaseID = ToSession(acEvent.ProjectileBaseID).value_or(GameId{});
    aLaunch.WeaponID = ToSession(acEvent.WeaponID).value_or(GameId{});
    aLaunch.AmmoID = ToSession(acEvent.AmmoID).value_or(GameId{});
    aLaunch.ZAngle = acEvent.ZAngle;
    aLaunch.XAngle = acEvent.XAngle;
    aLaunch.YAngle = acEvent.YAngle;
    aLaunch.ParentCellID = ToSession(acEvent.ParentCellID).value_or(GameId{});
    aLaunch.SpellID = ToSession(acEvent.SpellID).value_or(GameId{});
    aLaunch.CastingSource = acEvent.CastingSource;
    aLaunch.Area = acEvent.Area;
    aLaunch.Power = acEvent.Power;
    aLaunch.Scale = acEvent.Scale;
    aLaunch.AlwaysHit = acEvent.AlwaysHit;
    aLaunch.NoDamageOutsideCombat = acEvent.NoDamageOutsideCombat;
    aLaunch.AutoAim = acEvent.AutoAim;
    aLaunch.DeferInitialization = acEvent.DeferInitialization;
    aLaunch.ForceConeOfFire = acEvent.ForceConeOfFire;
    aLaunch.UnkBool1 = acEvent.UnkBool1;
    aLaunch.UnkBool2 = acEvent.UnkBool2;
}

inline void Launch(IFormBridge& aForms, uint32_t aShooterFormId, const Payloads::ProjectileLaunch& acLaunch) noexcept
{
    const uint32_t cParentCellId = ToGame(acLaunch.ParentCellID);
    if (!aForms.FormExists(cParentCellId))
    {
        spdlog::warn("Cannot launch projectile, invalid parent cell: {:X}", cParentCellId);
        return;
    }
    aForms.LaunchProjectile(aShooterFormId, cParentCellId, acLaunch.OriginX, acLaunch.OriginY, acLaunch.OriginZ,
                            ToGame(acLaunch.ProjectileBaseID), ToGame(acLaunch.WeaponID), ToGame(acLaunch.AmmoID),
                            ToGame(acLaunch.SpellID), acLaunch.CastingSource, acLaunch.ZAngle, acLaunch.XAngle, acLaunch.YAngle,
                            acLaunch.Area, acLaunch.Power, acLaunch.Scale, acLaunch.AlwaysHit, acLaunch.NoDamageOutsideCombat,
                            acLaunch.AutoAim, false /* DeferInitialization */, acLaunch.ForceConeOfFire, acLaunch.UnkBool1,
                            acLaunch.UnkBool2);
}
} // namespace ProjectileLaunches
