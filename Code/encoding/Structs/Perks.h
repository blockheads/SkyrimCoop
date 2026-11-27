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

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept
    {
        Wire::Serialization::WriteVarInt(aWriter, Ranks.size());
        for (const auto& cRank : Ranks)
        {
            cRank.Id.Serialize(aWriter);
            Wire::Serialization::WriteVarInt(aWriter, cRank.Rank);
        }
        Wire::Serialization::WriteVarInt(aWriter, Spells.size());
        for (const auto& cSpell : Spells)
            cSpell.Serialize(aWriter);
    }
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept
    {
        Ranks.clear();
        const uint64_t cCount = Wire::Serialization::ReadCount(aReader, kMaxEntries);
        for (uint64_t i = 0; i < cCount; ++i)
        {
            auto& rank = Ranks.emplace_back();
            rank.Id.Deserialize(aReader);
            rank.Rank = Wire::Serialization::ReadVarInt(aReader) & 0xFF;
        }
        Spells.clear();
        const uint64_t cSpells = Wire::Serialization::ReadCount(aReader, kMaxEntries);
        for (uint64_t i = 0; i < cSpells; ++i)
            Spells.emplace_back().Deserialize(aReader);
    }
};
