#pragma once

#include "Message.h"
#include "Payloads.h"

struct RequestActorValueChanges final : ClientMessage, Payloads::ActorValueChanges
{
    static constexpr ClientOpcode Opcode = kRequestActorValueChanges;
    using Payload = Payloads::ActorValueChanges;

    RequestActorValueChanges()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestActorValueChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
