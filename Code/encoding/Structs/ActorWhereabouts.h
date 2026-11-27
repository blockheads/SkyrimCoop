#pragma once

#include <Wire/Archive.h>

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Wire/Buffer.h>

// Where an owner's actor went when its owner stopped simulating it: its copies follow (copy-contract.md,
// "When the owner's actor goes away").
struct ActorWhereabouts
{
    enum class State : uint8_t
    {
        kUnknown, // nothing to follow (a player leaving, an older game)
        kPlaced,  // still in the world, at CellId (WorldSpaceId outdoors) and Position
        kDisabled,
        kDeleted,
    };

    bool operator==(const ActorWhereabouts& acRhs) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Wire::Bits<8>(Where));
        if (Where == State::kPlaced)
            aArchive(CellId, WorldSpaceId, Position, Heading);
    }

    COOP_SERIALIZE_FIELDS

    State Where{State::kUnknown};
    GameId CellId{};
    GameId WorldSpaceId{}; // empty: an interior
    Vector3_NetQuantize Position{};
    float Heading{};
};
