#pragma once

#include "Message.h"
#include "Payloads.h"

struct RequestActiveEffects final : ClientMessage, Payloads::ActiveEffects
{
    static constexpr ClientOpcode Opcode = kRequestActiveEffects;
    using Payload = Payloads::ActiveEffects;

    RequestActiveEffects()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestActiveEffects& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
