#pragma once

#include <Structs/PlayerRecord.h>
#include <Wire/Serialization.h>

// An owner's complete perk list, including ranks; an empty list removes the copy's old perks.
struct Perks
{
    std::vector<PlayerRecord::Perk> Ranks;
    bool operator==(const Perks&) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept
    {
        Wire::Serialization::WriteVarInt(aWriter, Ranks.size());
        for (const auto& cRank : Ranks)
        {
            cRank.Id.Serialize(aWriter);
            Wire::Serialization::WriteVarInt(aWriter, cRank.Rank);
        }
    }
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept
    {
        Ranks.clear();
        const uint64_t cCount = Wire::Serialization::ReadVarInt(aReader);
        for (uint64_t i = 0; i < cCount && i < 4096 && !aReader.Eof(); ++i)
        {
            auto& rank = Ranks.emplace_back();
            rank.Id.Deserialize(aReader);
            rank.Rank = Wire::Serialization::ReadVarInt(aReader) & 0xFF;
        }
    }
};
