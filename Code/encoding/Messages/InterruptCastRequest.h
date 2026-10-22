#pragma once

#include "Message.h"
#include "Payloads.h"

struct InterruptCastRequest final : RequestMessage, Payloads::InterruptCast
{
    static constexpr RequestOpcode Opcode = kInterruptCastRequest;
    using Payload = Payloads::InterruptCast;

    InterruptCastRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const InterruptCastRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
