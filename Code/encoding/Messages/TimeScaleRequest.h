#pragma once

#include "Message.h"

// How fast a player's game wants its clock to run (1 normal; a kill cam, Slow Time or a bow zoom slow it).
struct TimeScaleRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kTimeScaleRequest;

    TimeScaleRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const TimeScaleRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Multiplier == acRhs.Multiplier; }

    float Multiplier{1.f};
};
