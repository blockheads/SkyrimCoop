#include "Projectile.h"
#include <Games/Magic/CastingSafety.h>
#include <HookSwitch.h>
#include <Games/Skyrim/Forms/TESObjectWEAP.h>
#include <Games/Skyrim/Forms/MagicItem.h>
#include <Games/Skyrim/Forms/TESAmmo.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <World.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Games/Skyrim/Forms/TESObjectCELL.h>
#include <Forms/SpellItem.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Hooks/JitAssembly.h>
#include <RTTI.h>
#include <VersionDb.h>

using TLaunch = BSPointerHandle<Projectile>*(BSPointerHandle<Projectile>*, Projectile::LaunchData& arData);
static TLaunch* RealLaunch = nullptr;

BSPointerHandle<Projectile>* Projectile::Launch(BSPointerHandle<Projectile>* apResult, LaunchData& apLaunchData) noexcept
{
    BSPointerHandle<Projectile>* result = RealLaunch(apResult, apLaunchData);

    // Launch can fail, or return a handle whose deferred projectile is not available yet.
    if (!result)
    {
        spdlog::error("No projectile handle returned.");
        return nullptr;
    }

    TESObjectREFR* pObject = TESObjectREFR::GetByHandle(result->handle.iBits);
    Projectile* pProjectile = Cast<Projectile>(pObject);

    if (!pProjectile)
    {
        spdlog::error("No projectile found.");
        return nullptr;
    }

    pProjectile->fPower = apLaunchData.fPower;

    return result;
}

BSPointerHandle<Projectile>* HookLaunch(BSPointerHandle<Projectile>* apThis, Projectile::LaunchData& arData)
{
    // sync concentration spells through spell cast sync, the rest through projectile sync
    if (arData.pSpell)
    {
        if (auto* pSpell = Cast<SpellItem>(arData.pSpell))
        {
            if (pSpell->eCastingType == MagicSystem::CastingType::CONCENTRATION)
            {
                return RealLaunch(apThis, arData);
            }
        }
    }

    if (arData.pShooter)
    {
        Actor* pActor = Cast<Actor>(arData.pShooter);
        if (pActor)
        {
            ActorExtension* pExtendedActor = CastingSafety::ExtensionOf(pActor);
            if (pExtendedActor && pExtendedActor->IsCopy())
            {
                apThis->handle.iBits = 0;
                return apThis;
            }
        }
    }

    ProjectileLaunchedEvent Event{};
    Event.Origin = {arData.Origin.x, arData.Origin.y, arData.Origin.z};
    if (arData.pProjectileBase)
        Event.ProjectileBaseID = arData.pProjectileBase->formID;
    if (arData.pShooter)
        Event.ShooterID = arData.pShooter->formID;
    if (arData.pFromWeapon)
        Event.WeaponID = arData.pFromWeapon->formID;
    if (arData.pFromAmmo)
        Event.AmmoID = arData.pFromAmmo->formID;
    Event.ZAngle = arData.fZAngle;
    Event.XAngle = arData.fXAngle;
    Event.YAngle = arData.fYAngle;
    if (arData.pParentCell)
        Event.ParentCellID = arData.pParentCell->formID;
    if (arData.pSpell)
        Event.SpellID = arData.pSpell->formID;
    Event.CastingSource = arData.eCastingSource;
    Event.UnkBool1 = arData.bUnkBool1;
    Event.Area = arData.iArea;
    Event.Power = arData.fPower;
    Event.Scale = arData.fScale;
    Event.AlwaysHit = arData.bAlwaysHit;
    Event.NoDamageOutsideCombat = arData.bNoDamageOutsideCombat;
    Event.AutoAim = arData.bAutoAim;
    Event.UnkBool2 = arData.bUnkBool2;
    Event.DeferInitialization = arData.bDeferInitialization;
    Event.ForceConeOfFire = arData.bForceConeOfFire;

    return CastingSafety::WithProjectile(RealLaunch(apThis, arData), [](uint32_t aHandle) {
        return Cast<Projectile>(TESObjectREFR::GetByHandle(aHandle));
    }, [&Event](Projectile* apProjectile) {
        Event.Power = apProjectile->fPower;
        World::Get().GetRunner().Trigger(Event);
    });
}

static Hooks::Initializer s_projectileHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(44108, &RealLaunch, HookLaunch);

        VersionDbPtr<uint8_t> hookLoc(34452);

        struct C : Hooks::CodeGenerator
        {
            C(uint8_t* apLoc)
            {
                // replicate
                mov(rbx, ptr[rsp + 0x50]);

                // nullptr check
                cmp(rbx, 0);
                jz("exit");
                // jump back
                jmp_S(apLoc + 0x379);

                L("exit");
                // return false; scratch space from the registers
                mov(al, 0);
                add(rsp, 0x138);
                pop(r15);
                pop(r14);
                pop(r13);
                pop(r12);
                pop(rdi);
                pop(rsi);
                pop(rbx);
                pop(rbp);
                ret();
            }
        } gen(hookLoc.Get());
        Hooks::Jump(hookLoc.Get() + 0x374, gen.getCode());
    });
