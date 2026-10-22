#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyProjectileLaunch final : NotifyMessage, Payloads::ProjectileLaunch
{
    static constexpr NotifyOpcode Opcode = kNotifyProjectileLaunch;
    using Payload = Payloads::ProjectileLaunch;

    NotifyProjectileLaunch()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyProjectileLaunch& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
