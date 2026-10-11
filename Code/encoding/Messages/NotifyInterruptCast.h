#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyInterruptCast final : ServerMessage, Payloads::InterruptCast
{
    static constexpr ServerOpcode Opcode = kNotifyInterruptCast;
    using Payload = Payloads::InterruptCast;

    NotifyInterruptCast()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyInterruptCast& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
