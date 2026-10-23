#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct PlayerNativeRequest final : RequestMessage, Payloads::PlayerNative
{
    static constexpr RequestOpcode Opcode = kPlayerNativeRequest;
    using Payload = Payloads::PlayerNative;

    PlayerNativeRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const PlayerNativeRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
