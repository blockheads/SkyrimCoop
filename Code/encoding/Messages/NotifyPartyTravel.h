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

// The soft leash (party-travel.md): a guest outdoors out of the host's loaded grid hears a countdown, then goes to the
// host; back in time, or once the leash no longer applies (a trip, indoors, a player down), the countdown stops.
struct NotifyLeash final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyLeash;

    NotifyLeash() : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyLeash& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && State == acRhs.State && Seconds == acRhs.Seconds;
    }

    enum class Kind : uint8_t
    {
        kOff,  // the countdown stops, nothing said (a trip took over, indoors, a player down)
        kOut,  // out of the host's grid: the countdown starts
        kBack, // back in the host's grid: the countdown stops, and the player is told
    };
    Kind State{Kind::kOff};
    uint8_t Seconds{}; // kOut: the countdown's length
};
