#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/ActionEvent.h>

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
        return GetOpcode() == acRhs.GetOpcode() && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && LatestAction == acRhs.LatestAction && GraphState == acRhs.GraphState;
    }

    GameId CellId{};
    GameId WorldSpaceId{}; // empty: an interior
    Vector3_NetQuantize Position{};
    // The player as it arrives, for the copies others spawn of it now (the host's record of it is from when
    // it joined): its last action and its behaviour graphs' state.
    ActionEvent LatestAction{};
    TiltedPhoques::String GraphState{};
};
