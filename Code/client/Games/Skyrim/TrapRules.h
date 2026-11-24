#pragma once

#include <Games/Skyrim/HitRules.h>

// Whose a trap's contact and hit are, with no engine (docs/design/traps.md): the host runs the trap's script, each
// game judges what touched its own player. Checked by ClientTests; the hooks and GameTests' fake engine follow it.
namespace TrapRules
{
enum class Contact
{
    Raise,   // the engine's event, as it is
    AskHost, // a guest's own player: the host's script decides
    Drop,    // the toucher's own game judges it
};

// aFromRequest: a guest's contact, raised on the host for its copy.
[[nodiscard]] constexpr Contact OfContact(bool aInSession, bool aHosting, HitRules::Party aTarget, bool aFromRequest) noexcept
{
    using enum HitRules::Party;
    if (!aInSession || aFromRequest)
        return Contact::Raise;
    if (aHosting)
        return aTarget == OtherPlayer ? Contact::Drop : Contact::Raise;
    return aTarget == LocalPlayer ? Contact::AskHost : Contact::Drop;
}

enum class Hit
{
    Apply,    // the engine's hit here
    AskOwner, // the host's script hit a guest's copy: its game takes it
    Drop,     // a copy: its owner's game decides
};

[[nodiscard]] constexpr Hit OfHit(bool aInSession, bool aHosting, HitRules::Party aTarget) noexcept
{
    using enum HitRules::Party;
    if (!aInSession || aTarget == LocalPlayer || aTarget == Local)
        return Hit::Apply;
    return aHosting && aTarget == OtherPlayer ? Hit::AskOwner : Hit::Drop;
}

// An effect on this game's player cast by an object of the world (a trap's Spell.Cast): on a guest the host's game
// decides it and asks for it (MagicTarget's world cast), so its own copy of that cast applies nothing; its own script's
// does. One with no caster (the host's explosion or hazard, made here too) applies, as its dart's hit does.
[[nodiscard]] constexpr bool AppliesWorldEffect(bool aIsGuest, HitRules::Party aTarget, bool aCastByObject, bool aOwnScript) noexcept
{
    return !aIsGuest || aTarget != HitRules::Party::LocalPlayer || !aCastByObject || aOwnScript;
}

// A world cast (no caster: a hazard) of an effect this game's actor already has: skipped, as the hazard's own
// IsSpellTarget (43987) skips an actor already under it. The host's copy never takes the effect, so its hazard casts
// again at each tick; applied each time, the burns stacked.
[[nodiscard]] constexpr bool SkipsWorldEffect(bool aNoCaster, bool aAlreadyActive) noexcept
{
    return aNoCaster && aAlreadyActive;
}

// An animation the host's script plays on an object with no 3D does nothing there (its graph takes no event), so a
// script looping on it returns at once: sent when it differs from the last, not each time.
[[nodiscard]] inline bool SendsAnimation(bool aHas3D, bool aSameAsLast) noexcept
{
    return aHas3D || !aSameAsLast;
}
} // namespace TrapRules
