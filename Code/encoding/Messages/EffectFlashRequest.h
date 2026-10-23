#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct EffectFlashRequest final : RequestMessage, Payloads::EffectFlash
{
    static constexpr RequestOpcode Opcode = kEffectFlashRequest;
    using Payload = Payloads::EffectFlash;

    EffectFlashRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const EffectFlashRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
