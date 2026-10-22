#pragma once

#include "Message.h"
#include "Payloads.h"

struct RequestDeathStateChange final : RequestMessage, Payloads::DeathStateChange
{
    static constexpr RequestOpcode Opcode = kRequestDeathStateChange;
    using Payload = Payloads::DeathStateChange;

    RequestDeathStateChange()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestDeathStateChange& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
