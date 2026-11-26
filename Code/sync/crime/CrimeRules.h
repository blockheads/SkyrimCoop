#pragma once

// CrimeBridge::Judge's checks with no engine (docs/re/systems/crime.md, "A created item's theft names its owner"):
// UnitTests check them; the bridge and GameTests' fake follow them.
#include <Messages/CrimeMessages.h>
#include <cstdint>
#include <map>

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
// The steal warning (37556, procedure 40) names g_thePlayer: for a guest's theft, its copy in that place.
template <class T> [[nodiscard]] constexpr T Warned(T aPassed, T aPlayer, T aGuestCopy) noexcept
{
    return aGuestCopy && aPassed == aPlayer ? aGuestCopy : aPassed;
}

// The body each witness's pending crime (39557) was raised against: a guest's copy when Judge's scope set it. The
// witness's later look (39631) reads it instead of the engine's player (crime.md, "A hidden guest's theft").
class PendingCriminals
{
  public:
    // 39557 on a witness: a crime and the copy in scope, or 0 (the host's own crime, or the crime cleared).
    void Set(uintptr_t aWitness, uintptr_t aCrime, uint32_t aCopy)
    {
        if (aCrime && aCopy)
            m_entries[aWitness] = {aCrime, aCopy};
        else
            m_entries.erase(aWitness);
    }
    // The copy this witness's pending crime names, 0 to judge it as the engine does.
    [[nodiscard]] uint32_t Of(uintptr_t aWitness, uintptr_t aCrime) const
    {
        const auto cEntry = m_entries.find(aWitness);
        return aCrime && cEntry != m_entries.end() && cEntry->second.Crime == aCrime ? cEntry->second.Copy : 0;
    }
    // After 39631: the witness still holds `aCrime` (not yet judged) or not.
    void Judged(uintptr_t aWitness, uintptr_t aCrime)
    {
        if (!Of(aWitness, aCrime))
            m_entries.erase(aWitness);
    }
    void Clear() { m_entries.clear(); }
    [[nodiscard]] size_t Size() const { return m_entries.size(); }

  private:
    struct Entry
    {
        uintptr_t Crime;
        uint32_t Copy;
    };
    std::map<uintptr_t, Entry> m_entries;
};
} // namespace CrimeRules
