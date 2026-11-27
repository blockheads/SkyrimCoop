#pragma once

#include <Messages/Message.h>
#include <Messages/NotifyPartyTravel.h>
#include <Wire/Buffer.h>

// The native fast-travel time skipped by a guest's calendar, advanced on the host instead.
struct TravelTimeRequest final : MessageOf<TravelTimeRequest, kTravelTimeRequest>
{
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
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Hours); }
    bool operator==(const TravelTimeRequest&) const noexcept = default;
};

// The leader's queued destination, before loading it. Arrival still reports the actual destination.
struct PartyTravelRequest final : MessageOf<PartyTravelRequest, kPartyTravelRequest>
{
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
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Destination); }
    bool operator==(const PartyTravelRequest&) const noexcept = default;
};

// /tpa: everyone else comes onto the sender's spot.
struct PartySummonRequest final : MessageOf<PartySummonRequest, kPartySummonRequest>
{
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
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Destination); }
    bool operator==(const PartySummonRequest&) const noexcept = default;
};
