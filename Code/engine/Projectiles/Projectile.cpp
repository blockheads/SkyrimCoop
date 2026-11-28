#include "Projectiles/Projectile.h"
#include <Combat/HitOwnership.h>
#include <magic/CastingSafety.h>
#include <HookSwitch.h>
#include <Forms/TESObjectWEAP.h>
#include <Forms/MagicItem.h>
#include <Forms/TESAmmo.h>
#include <Actor.h>
#include <ActorExtension.h>
#include <core/World.h>
#include <combat/TrapRules.h>
#include <magic/ProjectileLaunchedEvent.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/SpellItem.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Hooks/JitAssembly.h>
#include <RTTI.h>
#include <VersionDb.h>
#include <core/Counters.h>

#include <format>

using TLaunch = BSPointerHandle<Projectile>*(BSPointerHandle<Projectile>*, Projectile::LaunchData& arData);
static TLaunch* RealLaunch = nullptr;

namespace
{
// Projectiles by base, the harness's readout of what each game showed: its engine's ("projectile E4CB5") and those
// relaunched for another game ("projectile E4CB5 relaunched").
void CountLaunch(const Projectile* apProjectile, const char* acpSuffix) noexcept
{
    if (apProjectile && apProjectile->baseForm)
        Diagnostics::Counters::Add(
            Diagnostics::Counters::Get(std::format("projectile {:X}{}", apProjectile->baseForm->formID, acpSuffix).c_str()), 1);
}
} // namespace

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
    CountLaunch(pProjectile, " relaunched");

    return result;
}

BSPointerHandle<Projectile>* HookLaunch(BSPointerHandle<Projectile>* apThis, Projectile::LaunchData& arData)
{
    // An actor's concentration spell goes by the cast sync (its copy casts it); the rest, an object's stream of flames
    // included, by the launch.
    const auto* pSpell = Cast<SpellItem>(arData.pSpell);
    if (!TrapRules::SendsLaunch(pSpell && pSpell->eCastingType == MagicSystem::CastingType::CONCENTRATION, Cast<Actor>(arData.pShooter)))
        return RealLaunch(apThis, arData);

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
    Event.FromObject = !Cast<Actor>(arData.pShooter);
    if (arData.pParentCell && arData.pParentCell->worldspace)
        Event.WorldSpaceID = arData.pParentCell->worldspace->formID;
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
        CountLaunch(apProjectile, "");
        Event.Power = apProjectile->fPower;
        World::Runner().Trigger(Event);
    });
}

namespace
{
thread_local bool t_worldImpact = false;

// Projectile::HandleHits (44204, vfunc 0xBE of every projectile type): its step's cast hits, in order. A weapon's
// enchantment is cast in HandleRefImpact (44205) by the hit actor's own caster; shot by no actor, that is the world's.
using TImpact = bool(Projectile*, void*);
TImpact* RealImpact = nullptr;

bool HookImpact(Projectile* apThis, void* apHit)
{
    Actor* pShooter = Cast<Actor>(TESObjectREFR::GetByHandle(apThis->hShooter));
    // apHit: the step's hkpAllCdPointCollector; the shooter's fellow rider is passed through (HitRules::RideTogether).
    if (pShooter)
        HitRules::DropFellowRiderHits(apHit, *pShooter);
    const bool cBefore = t_worldImpact;
    t_worldImpact = !pShooter;
    const bool cResult = RealImpact(apThis, apHit);
    t_worldImpact = cBefore;
    return cResult;
}
} // namespace

bool Projectile::WorldImpact() noexcept
{
    return t_worldImpact;
}

static Hooks::Initializer s_projectileHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(44108, &RealLaunch, HookLaunch);
        HOOK_ID(44204, &RealImpact, HookImpact);

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
