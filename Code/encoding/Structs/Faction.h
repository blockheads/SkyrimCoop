#pragma once

#include <Wire/Archive.h>

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct Faction
{
    Faction() = default;
    ~Faction() = default;

    bool operator==(const Faction&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id, Wire::Bits<8>(Rank)); }

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    GameId Id;
    int8_t Rank{};
};
