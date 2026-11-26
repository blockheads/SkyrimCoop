#pragma once

#include <Structs/Inventory.h>
#include <magic/OwnerEffects.h>
#include <vector>
#include <cstdint>

// An actor's spells, shouts and the effects on it. One part of IActorBridge (ports/IActorBridge.h).
struct IActorMagic
{
    virtual ~IActorMagic() = default;

    virtual void ActorEquipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlot) noexcept = 0;
    virtual void ActorEquipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept = 0;
    virtual void GenerateMagicCasters(uint32_t aFormId) noexcept = 0;
    virtual void SetMagicCasterDualCasting(uint32_t aFormId, uint32_t aCastingSource, bool aDual) noexcept = 0;
    /// Casts immediately from the actor's caster for that source (remote spell cast sync);
    /// aDesiredTargetFormId 0 = no target.
    virtual void CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource,
                           uint32_t aDesiredTargetFormId) noexcept = 0;
    virtual void InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept = 0;
    /// Spell currently being cast from that source (0 if none).
    virtual uint32_t GetCastingSpellFormId(uint32_t aFormId, uint32_t aCastingSource) const noexcept = 0;
    virtual void RemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId) noexcept = 0;
    virtual uint32_t GetEquippedShoutFormId(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetActiveMagicItemFormId(uint32_t aFormId, uint32_t aSlot) const noexcept = 0;
    // One active magic effect on an actor (debugging / desync checks).
    struct ActiveEffectInfo
    {
        uint32_t EffectFormId{};
        uint32_t SpellFormId{};
        uint32_t CasterFormId{};
        bool CasterIsActor{}; // false for no caster or an object's (a trap, an explosion, a hazard)
        float Magnitude{};
        float Duration{};
        float Elapsed{};
        uint32_t Flags{};
    };
    virtual std::vector<ActiveEffectInfo> GetActorActiveEffects(uint32_t aFormId) const noexcept = 0;
    virtual Inventory GetActorInventory(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept = 0;
    // Whether that magic effect shows on its target while it runs (a hit shader or hit art, FX Persist).
    virtual bool EffectHasHitVisuals(uint32_t aEffectFormId) const noexcept = 0;
    // A copy's owner's active effects, its answer to "has this effect / spell" (OwnerEffects.h).
    virtual void SetCopyEffects(uint32_t aFormId, std::vector<OwnerEffects::Held> aHeld) noexcept = 0;
    // Plays only that effect's hit visuals on the actor until stopped; true if they now play (not
    // before the actor has 3D, nor the first time a shader is used in this game: it loads).
    virtual bool PlayEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept = 0;
    virtual void StopEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept = 0;
    virtual bool IsPlayingEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) const noexcept = 0;
    // Plays that effect's hit visuals once on the actor, as its start does when it is not FX Persist (each runs its
    // own pass and ends); false if it has none or the actor has no 3D.
    virtual bool FlashEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept = 0;
};
