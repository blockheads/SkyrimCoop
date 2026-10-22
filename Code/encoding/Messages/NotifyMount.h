#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyMount final : NotifyMessage, Payloads::Mount
{
    static constexpr NotifyOpcode Opcode = kNotifyMount;
    using Payload = Payloads::Mount;

    NotifyMount()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyMount& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
