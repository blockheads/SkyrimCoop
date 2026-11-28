// TESObjectREFR's damage to a destructible: a guest's own goes to the host (docs/re/systems/destruction.md).
#include <HookSwitch.h>
#include <PlayerHands.h>
#include <TESObjectREFR.h>
#include <core/World.h>
#include <items/Overrides.h>
#include <world/DestructionEvent.h>

#include <ExtraData/ExtraObjectHealth.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

namespace
{
// TESObjectREFR::DamageObject (19526, vtable 0x42, `(this, damage, allowDestroyed)`): a swing (38628), a projectile,
// an explosion, a script's DamageObject; the health goes in ExtraObjectHealth (BGSDestructibleObjectForm 14158).
using TDamageObject = void(TESObjectREFR*, float, bool);
TDamageObject* RealDamageObject = nullptr;

float Health(const TESObjectREFR& acObject) noexcept
{
    const auto* pHealth = static_cast<const ExtraObjectHealth*>(acObject.extraData.GetByType(ExtraDataType::ObjectHealth));
    return pHealth ? pHealth->health : -1.f;
}

void HookDamageObject(TESObjectREFR* apThis, float aDamage, bool aAllowDestroyed)
{
    // A copy's swing runs its hit frame here too (38603 -> 38628): its owner's game damages, and sends the row.
    if (PlayerHands::CopySwings())
        return;
    const float cBefore = apThis ? Health(*apThis) : -1.f;
    RealDamageObject(apThis, aDamage, aAllowDestroyed);
    if (apThis && !ScopedHostChangeOverride::IsOverriden() && Health(*apThis) != cBefore)
        World::Runner().Trigger(DestructionEvent{apThis->formID});
}

Hooks::Initializer s_destructionHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(19526, &RealDamageObject, HookDamageObject);
});
} // namespace
