#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Wire/Buffer.h>

#include <string>

// The party tether: a player travelled by itself (EnterCellRequest::Travel::kOwn) and everyone else goes where it
// arrived. A game already there stays.
struct NotifyPartyTravel final : MessageOf<NotifyPartyTravel, kNotifyPartyTravel>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(CellId, WorldSpaceId, Position, Heading);
    }
    bool operator==(const NotifyPartyTravel&) const noexcept = default;

    GameId CellId{};
    GameId WorldSpaceId{}; // empty: an interior
    Vector3_NetQuantize Position{};
    float Heading{}; // the leader's on arrival
};

// /tpa: a player brought everyone to it; each game goes there even when already in that cell.
struct NotifyPartySummon final : MessageOf<NotifyPartySummon, kNotifyPartySummon>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Destination, Username); }
    bool operator==(const NotifyPartySummon&) const noexcept = default;

    NotifyPartyTravel Destination;
    std::string Username; // who summoned
};

// The soft leash (party-travel.md): a guest outdoors out of the host's loaded grid hears a countdown, then goes to the
// host; back in time, or once the leash no longer applies (a trip, indoors, a player down), the countdown stops.
struct NotifyLeash final : MessageOf<NotifyLeash, kNotifyLeash>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Wire::Bits<2>(State), Wire::Bits<8>(Seconds));
        if constexpr (TArchive::kReading)
            if (State > Kind::kBack)
                State = Kind::kOff;
    }
    bool operator==(const NotifyLeash&) const noexcept = default;

    enum class Kind : uint8_t
    {
        kOff,  // the countdown stops, nothing said (a trip took over, indoors, a player down)
        kOut,  // out of the host's grid: the countdown starts
        kBack, // back in the host's grid: the countdown stops, and the player is told
    };
    Kind State{Kind::kOff};
    uint8_t Seconds{}; // kOut: the countdown's length
};
