#pragma once

#include <Games/Skyrim/HitRules.h>

#include <chrono>

// Whose a trap's contact and hit are, with no engine (docs/design/traps.md): the host runs the trap's script, each
// game judges what touched its own player. Checked by UnitTests; the hooks and GameTests' fake engine follow it.
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

// A launch raised for the others: all but an actor's concentration spell, which its copy casts (the cast sync). An
// object's stream of flames goes by the launch, each puff its own (44108; flames_hurt_both: 117 a 7 s round).
[[nodiscard]] constexpr bool SendsLaunch(bool aConcentration, bool aActorShooter) noexcept
{
    return !(aConcentration && aActorShooter);
}

// A contact's velocity and position, but not a stop's: ContactRemoved (26068) leaves them stack scratch.
[[nodiscard]] constexpr bool CarriesVectors(uint32_t aType) noexcept
{
    return aType != 1; // TESTrapHitEvent's stop
}

// The host's world cast of an effect this actor already has from no actor: skipped. Two of its hazard's ticks can go
// out before its copy answers with the effect (IsSpellTarget, 43987).
[[nodiscard]] constexpr bool SkipsWorldEffect(bool aWorldCast, bool aAlreadyActive) noexcept
{
    return aWorldCast && aAlreadyActive;
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

// A guest's own copy of a world cast on its player (no actor casts it: a trap's spell, a hazard, an explosion, a
// projectile an object shot) applies nothing: the host's verdict asks for it. Its own script's does.
[[nodiscard]] constexpr bool AppliesWorldEffect(bool aIsGuest, HitRules::Party aTarget, bool aNoActorCaster, bool aOwnScript) noexcept
{
    return !aIsGuest || aTarget != HitRules::Party::LocalPlayer || !aNoActorCaster || aOwnScript;
}

// A script looping on an object with no 3D here (its graph takes no event) repeats at once: a repeat this soon is not sent.
inline constexpr std::chrono::milliseconds cAnimationRepeat{1000};

[[nodiscard]] constexpr bool SendsAnimation(bool aHas3D, bool aSameAsLast, std::chrono::milliseconds aSinceLast) noexcept
{
    return aHas3D || !aSameAsLast || aSinceLast >= cAnimationRepeat;
}
} // namespace TrapRules
