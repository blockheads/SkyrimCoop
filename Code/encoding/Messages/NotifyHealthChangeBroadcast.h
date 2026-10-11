#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyHealthChangeBroadcast final : ServerMessage, Payloads::HealthChange
{
    static constexpr ServerOpcode Opcode = kNotifyHealthChangeBroadcast;
    using Payload = Payloads::HealthChange;

    NotifyHealthChangeBroadcast()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyHealthChangeBroadcast& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
