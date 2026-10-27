#pragma once

#include <Messages/Message.h>
#include <Messages/NotifyPartyTravel.h>
#include <Wire/Buffer.h>

// The native fast-travel time skipped by a guest's calendar, advanced on the host instead.
struct TravelTimeRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kTravelTimeRequest;
    TravelTimeRequest() : RequestMessage(Opcode)
    {
    }
    float Hours{};
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override
    {
        Serialization::WriteFloat(aWriter, Hours);
    }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override
    {
        RequestMessage::DeserializeRaw(aReader);
        Hours = Serialization::ReadFloat(aReader);
    }
    bool operator==(const TravelTimeRequest& acOther) const noexcept
    {
        return Hours == acOther.Hours;
    }
};

// The leader's queued destination, before loading it. Arrival still reports the actual destination.
struct PartyTravelRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kPartyTravelRequest;
    PartyTravelRequest() : RequestMessage(Opcode)
    {
    }
    NotifyPartyTravel Destination;
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override
    {
        Destination.SerializeRaw(aWriter);
    }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override
    {
        RequestMessage::DeserializeRaw(aReader);
        Destination.DeserializeRaw(aReader);
    }
    bool operator==(const PartyTravelRequest& acOther) const noexcept
    {
        return Destination == acOther.Destination;
    }
};
