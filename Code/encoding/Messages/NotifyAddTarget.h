#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyAddTarget final : NotifyMessage, Payloads::AddTarget
{
    static constexpr NotifyOpcode Opcode = kNotifyAddTarget;
    using Payload = Payloads::AddTarget;

    NotifyAddTarget()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyAddTarget& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
