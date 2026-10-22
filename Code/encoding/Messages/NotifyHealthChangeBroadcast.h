#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyHealthChangeBroadcast final : NotifyMessage, Payloads::HealthChange
{
    static constexpr NotifyOpcode Opcode = kNotifyHealthChangeBroadcast;
    using Payload = Payloads::HealthChange;

    NotifyHealthChangeBroadcast()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyHealthChangeBroadcast& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
