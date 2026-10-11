#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyAddTarget final : ServerMessage, Payloads::AddTarget
{
    static constexpr ServerOpcode Opcode = kNotifyAddTarget;
    using Payload = Payloads::AddTarget;

    NotifyAddTarget()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyAddTarget& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
