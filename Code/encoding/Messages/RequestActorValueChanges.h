#pragma once

#include "Message.h"
#include "Payloads.h"

struct RequestActorValueChanges final : RequestMessage, Payloads::ActorValueChanges
{
    static constexpr RequestOpcode Opcode = kRequestActorValueChanges;
    using Payload = Payloads::ActorValueChanges;

    RequestActorValueChanges()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestActorValueChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
