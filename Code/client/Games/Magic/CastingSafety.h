#pragma once

// Null-safe steps of the casting hooks (ActorMagicCaster, MagicTarget, Projectile).
namespace CastingSafety
{
template <class TActor> auto ExtensionOf(TActor* apActor) noexcept -> decltype(apActor->GetExtension())
{
    return apActor ? apActor->GetExtension() : nullptr;
}

template <class Caster, class Spell> Spell* SpellFor(Caster& aCaster, Spell* apSpell) noexcept
{
    return apSpell ? apSpell : aCaster.pCurrentSpell;
}

// Keep the native launch result even when it failed or its deferred projectile cannot yet be resolved.
template <class Handle, class Resolve, class Notify>
Handle* WithProjectile(Handle* apResult, Resolve&& aResolve, Notify&& aNotify)
{
    if (apResult)
        if (auto* pProjectile = aResolve(apResult->handle.iBits))
            aNotify(pProjectile);
    return apResult;
}
} // namespace CastingSafety
