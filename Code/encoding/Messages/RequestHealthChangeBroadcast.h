#pragma once

#include "Message.h"
#include "Payloads.h"

struct RequestHealthChangeBroadcast final : ClientMessage, Payloads::HealthChange
{
    static constexpr ClientOpcode Opcode = kRequestHealthChangeBroadcast;
    using Payload = Payloads::HealthChange;

    RequestHealthChangeBroadcast()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestHealthChangeBroadcast& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
