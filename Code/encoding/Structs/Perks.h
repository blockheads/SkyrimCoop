#pragma once

#include <Wire/Archive.h>

#include <Structs/PlayerRecord.h>
#include <Wire/Serialization.h>

// An owner's complete perk list, including ranks, and the spells added to it (HasSpell); an empty list removes the
// copy's old ones.
struct Perks
{
    static constexpr uint64_t kMaxEntries = 4096;

    std::vector<PlayerRecord::Perk> Ranks;
    std::vector<GameId> Spells;
    bool operator==(const Perks&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Wire::List(Ranks, kMaxEntries), Wire::List(Spells, kMaxEntries)); }

    COOP_SERIALIZE_FIELDS
};
