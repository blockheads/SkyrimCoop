#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct PlayerNativeResultRequest final : RequestMessage, Payloads::PlayerNativeResult
{
    static constexpr RequestOpcode Opcode = kPlayerNativeResultRequest;
    using Payload = Payloads::PlayerNativeResult;

    PlayerNativeResultRequest() : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override
    {
        Payload::SerializePayload(aWriter);
    }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override
    {
        Payload::DeserializePayload(aReader);
    }

    bool operator==(const PlayerNativeResultRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs);
    }
};
