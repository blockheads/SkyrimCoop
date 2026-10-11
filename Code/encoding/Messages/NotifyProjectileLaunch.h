#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyProjectileLaunch final : ServerMessage, Payloads::ProjectileLaunch
{
    static constexpr ServerOpcode Opcode = kNotifyProjectileLaunch;
    using Payload = Payloads::ProjectileLaunch;

    NotifyProjectileLaunch()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyProjectileLaunch& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
