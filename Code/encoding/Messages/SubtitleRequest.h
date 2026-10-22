#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct SubtitleRequest final : RequestMessage, Payloads::Subtitle
{
    static constexpr RequestOpcode Opcode = kSubtitleRequest;
    using Payload = Payloads::Subtitle;

    SubtitleRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const SubtitleRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
