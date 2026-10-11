#pragma once

#include "Message.h"
#include "Payloads.h"

struct RequestDeathStateChange final : ClientMessage, Payloads::DeathStateChange
{
    static constexpr ClientOpcode Opcode = kRequestDeathStateChange;
    using Payload = Payloads::DeathStateChange;

    RequestDeathStateChange()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestDeathStateChange& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
