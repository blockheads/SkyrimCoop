#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct RequestActorMaxValueChanges final : RequestMessage, Payloads::ActorMaxValueChanges
{
    static constexpr RequestOpcode Opcode = kRequestActorMaxValueChanges;
    using Payload = Payloads::ActorMaxValueChanges;

    RequestActorMaxValueChanges()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestActorMaxValueChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
