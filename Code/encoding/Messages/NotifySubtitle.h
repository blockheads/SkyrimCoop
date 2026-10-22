#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifySubtitle final : NotifyMessage, Payloads::Subtitle
{
    static constexpr NotifyOpcode Opcode = kNotifySubtitle;
    using Payload = Payloads::Subtitle;

    NotifySubtitle()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifySubtitle& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
