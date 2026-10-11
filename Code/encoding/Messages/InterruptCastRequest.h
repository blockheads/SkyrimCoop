#pragma once

#include "Message.h"
#include "Payloads.h"

struct InterruptCastRequest final : ClientMessage, Payloads::InterruptCast
{
    static constexpr ClientOpcode Opcode = kInterruptCastRequest;
    using Payload = Payloads::InterruptCast;

    InterruptCastRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const InterruptCastRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
