#pragma once

#include <combat/HitRules.h>
#include <combat/TrapRules.h>

#include <cstdint>

// HookAddTarget's rule with no engine (MagicTarget::AddTarget 34526): what this game does with an effect about to land
// on an actor. The caster's game decides a spell hit; only the target's owner applies it (docs/design/friendly-fire.md).
// Checked by UnitTests.
namespace EffectRules
{
using HitRules::Party;

// What the hook read from the engine.
struct Landing
{
    Party Target = Party::Local;
    Party Caster = Party::None; // None: no actor casts it
    bool TargetIsThisPlayer{};  // the engine's own player (PlayerCharacter::Get), in a session or not
    bool CasterIsTarget{};
    bool Guest{};        // this game is a guest in a session
    bool Hosting{};
    bool Forbidden{};    // this player's own effect, never shared (night vision)
    bool Overridden{};   // another game's request being applied here (ScopedSpellCastOverride)
    bool Summon{};       // the effect summons
    bool Shared{};       // AddTargetData::ShouldSync: not a summon, invisibility or ward
    bool Healing{};
    bool Buff{};
    bool WorldImpact{};  // an object's projectile's enchantment (Projectile::WorldImpact)
    uint32_t ScriptFor{}; // the player a running script's native acts for (GuestScripts::NativeGuest), 0x14: this game's
};

struct Verdict
{
    bool Apply{};     // the engine's own AddTarget runs
    bool AskOwner{};  // AddTargetEvent: the target's owner applies it
    bool HealPerks{}; // the request carries the caster's healing perks
    bool Hit{};       // HitEvent: the caster hit the target
    bool Summon{};    // SummonEvent, when the engine took the effect
    bool operator==(const Verdict&) const = default;
};

[[nodiscard]] constexpr bool IsCopy(Party aParty) noexcept
{
    return aParty == Party::OtherPlayer || aParty == Party::Copy;
}

// The host's world on a guest's character: no actor casts it (a statue, a trap, a hazard, an explosion), or a script
// making a copy cast for a guest. The host's verdict, sent to the guest's game.
[[nodiscard]] constexpr bool IsWorldCast(const Landing& acLanding) noexcept
{
    if (acLanding.Target != Party::OtherPlayer || !acLanding.Hosting)
        return false;
    const Party cCaster = acLanding.WorldImpact ? Party::None : acLanding.Caster;
    return cCaster == Party::None || (IsCopy(cCaster) && acLanding.ScriptFor && acLanding.ScriptFor != 0x14);
}

[[nodiscard]] constexpr Verdict Of(const Landing& acLanding) noexcept
{
    const bool cHasCaster = acLanding.Caster != Party::None;
    if (acLanding.Forbidden || acLanding.Overridden)
        return {.Apply = true};

    // A guest's player's own summon: its engine takes the effect, the host's makes the creature (summons.md).
    if (acLanding.Guest && acLanding.Summon && acLanding.TargetIsThisPlayer && acLanding.CasterIsTarget)
        return {.Apply = true, .Summon = true};

    // A copy never applies an effect itself: it shows its owner's state.
    if (IsCopy(acLanding.Target))
    {
        const bool cOurCast = cHasCaster && !IsCopy(acLanding.Caster);
        // Someone else's actor hit by someone else's spell: their games handle it.
        if (!cOurCast && !IsWorldCast(acLanding))
            return {};
        return {.AskOwner = acLanding.Shared,
                .HealPerks = acLanding.Target == Party::OtherPlayer && acLanding.Healing && cHasCaster,
                .Hit = cHasCaster && !acLanding.WorldImpact && !acLanding.Healing && !acLanding.Buff};
    }

    // On a guest, its own copy of a world cast on its player: the host's game decides and asks.
    if (!TrapRules::AppliesWorldEffect(acLanding.Guest, acLanding.Target, !cHasCaster || acLanding.WorldImpact, acLanding.ScriptFor == 0x14))
        return {};

    // Ours, hit by a copy's replayed spell: its caster's game already asked us. Only the hit counts (combat targeting).
    if (IsCopy(acLanding.Caster))
        return {.Hit = true};

    return {.Apply = true};
}
} // namespace EffectRules
