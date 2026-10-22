#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyInterruptCast final : NotifyMessage, Payloads::InterruptCast
{
    static constexpr NotifyOpcode Opcode = kNotifyInterruptCast;
    using Payload = Payloads::InterruptCast;

    NotifyInterruptCast()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyInterruptCast& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
