#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct NotifyPlayerNativeResult final : NotifyMessage, Payloads::PlayerNativeResult
{
    static constexpr NotifyOpcode Opcode = kNotifyPlayerNativeResult;
    using Payload = Payloads::PlayerNativeResult;

    NotifyPlayerNativeResult() : NotifyMessage(Opcode)
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

    bool operator==(const NotifyPlayerNativeResult& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs);
    }
};
