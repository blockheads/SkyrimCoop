#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

// A player's game arrived somewhere new (a door, coc, fast travel): its copies of everyone else
// went with the old cell. The host sends it what is there and tells those there about it
// (HostCharacters::OnEnterCell).
struct EnterCellRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kEnterCellRequest;

    EnterCellRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const EnterCellRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position;
    }

    GameId CellId{};
    GameId WorldSpaceId{}; // empty: an interior
    Vector3_NetQuantize Position{};
};
