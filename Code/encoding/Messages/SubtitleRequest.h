#pragma once

#include "Message.h"
#include "Payloads.h"

struct SubtitleRequest final : ClientMessage, Payloads::Subtitle
{
    static constexpr ClientOpcode Opcode = kSubtitleRequest;
    using Payload = Payloads::Subtitle;

    SubtitleRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const SubtitleRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
