#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyMount final : ServerMessage, Payloads::Mount
{
    static constexpr ServerOpcode Opcode = kNotifyMount;
    using Payload = Payloads::Mount;

    NotifyMount()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyMount& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
