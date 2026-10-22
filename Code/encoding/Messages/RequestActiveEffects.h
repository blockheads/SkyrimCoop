#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct RequestActiveEffects final : RequestMessage, Payloads::ActiveEffects
{
    static constexpr RequestOpcode Opcode = kRequestActiveEffects;
    using Payload = Payloads::ActiveEffects;

    RequestActiveEffects()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestActiveEffects& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
