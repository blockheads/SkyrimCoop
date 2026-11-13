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

// What an NPC holds against the party (crime.md, "Who an NPC fights for a party crime").
struct Grudge
{
    bool FactionEnemy = false;    // its crime faction's kPlayerIsEnemy (TESFaction +0x58 bit 3)
    bool Victim = false;          // a crime against it on its kPlayerCrimeList (extra 0x34)
    bool AngryWithPlayer = false; // kAngryWithPlayer (Actor +0x204 bit 0x800, 37463): a theft's witness or owner
    bool Witness = false;         // on a party crime's witness list (KnownCrime 41354): every alarm and guard responder
};

// Hostile for a party crime: then every player is as guilty and the NPC goes for the nearest.
[[nodiscard]] constexpr bool PartyIsCriminalTo(const Grudge& acGrudge) noexcept
{
    return acGrudge.FactionEnemy || acGrudge.Victim || acGrudge.AngryWithPlayer || acGrudge.Witness;
}

// The owner StealAlarm gets: the one the guest's alarm was given, else the target's own.
template <class T> [[nodiscard]] constexpr T AlarmOwner(T aGiven, T aTargetOwner) noexcept
{
    return aGiven ? aGiven : aTargetOwner;
}
} // namespace CrimeRules
