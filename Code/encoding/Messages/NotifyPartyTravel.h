#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Wire/Buffer.h>

#include <string>

// The party tether: a player travelled by itself (EnterCellRequest::Travel::kOwn) and everyone else goes where it
// arrived. A game already there stays.
struct NotifyPartyTravel final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPartyTravel;

    NotifyPartyTravel() : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyTravel& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId &&
               Position == acRhs.Position && Heading == acRhs.Heading;
    }

    GameId CellId{};
    GameId WorldSpaceId{}; // empty: an interior
    Vector3_NetQuantize Position{};
    float Heading{}; // the leader's on arrival
};

// /tpa: a player brought everyone to it; each game goes there even when already in that cell.
struct NotifyPartySummon final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPartySummon;

    NotifyPartySummon() : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartySummon& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Destination == acRhs.Destination && Username == acRhs.Username;
    }

    NotifyPartyTravel Destination;
    std::string Username; // who summoned
};
