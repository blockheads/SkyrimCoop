#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct SetTimeCommandRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kSetTimeCommandRequest;

    SetTimeCommandRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const SetTimeCommandRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Hours == acRhs.Hours && Minutes == acRhs.Minutes && PlayerId == acRhs.PlayerId; }

    uint8_t Hours{};
    uint8_t Minutes{};
    uint32_t PlayerId{};
};
