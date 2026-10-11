#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifySubtitle final : ServerMessage, Payloads::Subtitle
{
    static constexpr ServerOpcode Opcode = kNotifySubtitle;
    using Payload = Payloads::Subtitle;

    NotifySubtitle()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifySubtitle& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
