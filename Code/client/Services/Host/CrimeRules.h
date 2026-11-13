#pragma once

// CrimeBridge::Judge's checks with no engine (docs/re/systems/crime.md, "A created item's theft names its owner"):
// ClientTests check them; the bridge and GameTests' fake follow them.
#include <Messages/CrimeMessages.h>

namespace CrimeRules
{
// What Judge checks of a guest's crime's target before the native call.
struct Checks
{
    bool SameCell = true;  // in the guest's cell
    bool Ownership = true; // not the party's (IsAnOwner 20210)
};

// A theft naming an actor names a created item's owner (40533, any loaded cell): neither applies, an NPC reads as
// the player's. A pickpocket's own alarm decides ownership.
[[nodiscard]] constexpr Checks ChecksFor(CrimeAction aAction, bool aTargetIsActor) noexcept
{
    if (aAction == CrimeAction::Theft && aTargetIsActor)
        return {.SameCell = false, .Ownership = false};
    return {.SameCell = true, .Ownership = aAction != CrimeAction::Pickpocket};
}

// The owner StealAlarm gets: the one the guest's alarm was given, else the target's own.
template <class T> [[nodiscard]] constexpr T AlarmOwner(T aGiven, T aTargetOwner) noexcept
{
    return aGiven ? aGiven : aTargetOwner;
}
// A steal warning (StartStealWarning 37556) goes to the host's player alone: a guest's seen theft is never warned.
[[nodiscard]] constexpr bool Warns(bool aAllowed, bool aByGuest) noexcept
{
    return aAllowed && !aByGuest;
}
} // namespace CrimeRules
