#pragma once

#include <Games/Skyrim/HitRules.h>

#include <chrono>

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

// A guest's own copy of an object's cast on its player (a trap's Spell.Cast) applies nothing: the host's asks for it.
[[nodiscard]] constexpr bool AppliesWorldEffect(bool aIsGuest, HitRules::Party aTarget, bool aCastByObject, bool aOwnScript) noexcept
{
    return !aIsGuest || aTarget != HitRules::Party::LocalPlayer || !aCastByObject || aOwnScript;
}

// A world cast (no caster) of an effect this game's actor already has: skipped, as a hazard's IsSpellTarget (43987) does.
[[nodiscard]] constexpr bool SkipsWorldEffect(bool aNoCaster, bool aAlreadyActive) noexcept
{
    return aNoCaster && aAlreadyActive;
}

// A script looping on an object with no 3D here (its graph takes no event) repeats at once: a repeat this soon is not sent.
inline constexpr std::chrono::milliseconds cAnimationRepeat{1000};

[[nodiscard]] constexpr bool SendsAnimation(bool aHas3D, bool aSameAsLast, std::chrono::milliseconds aSinceLast) noexcept
{
    return aHas3D || !aSameAsLast || aSinceLast >= cAnimationRepeat;
}
} // namespace TrapRules
