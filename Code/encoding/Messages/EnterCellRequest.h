#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/ActionEvent.h>
#include <Structs/ActorData.h>

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
        return GetOpcode() == acRhs.GetOpcode() && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId &&
               Position == acRhs.Position && Heading == acRhs.Heading && How == acRhs.How && LatestAction == acRhs.LatestAction &&
               GraphState == acRhs.GraphState && Data == acRhs.Data;
    }

    GameId CellId{};
    GameId WorldSpaceId{}; // empty: an interior
    Vector3_NetQuantize Position{};
    float Heading{}; // the way it faces on arrival (radians clockwise from north): followers face it too
    // How it got here: its own travel, which everyone else follows (NotifyPartyTravel); the party tether's; or a
    // travel from before it joined, which neither leads nor follows.
    enum class Travel : uint8_t
    {
        kOwn,
        kTether,
        kBeforeJoining,
    };
    Travel How{Travel::kOwn};
    // The player as it arrives, for the copies others spawn of it now (the host's record of it is from when
    // it joined): its last action, its behaviour graphs' state, its inventory and values.
    ActionEvent LatestAction{};
    TiltedPhoques::String GraphState{};
    ActorData Data{};
};
