#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct SetTimeCommandRequest final : MessageOf<SetTimeCommandRequest, kSetTimeCommandRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Wire::Bits<8>(Hours), Wire::Bits<8>(Minutes), Wire::Bits<32>(PlayerId));
    }
    bool operator==(const SetTimeCommandRequest&) const noexcept = default;

    uint8_t Hours{};
    uint8_t Minutes{};
    uint32_t PlayerId{};
};
