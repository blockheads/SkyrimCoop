#pragma once

#include "Message.h"
#include "Payloads.h"

struct RequestActorMaxValueChanges final : ClientMessage, Payloads::ActorMaxValueChanges
{
    static constexpr ClientOpcode Opcode = kRequestActorMaxValueChanges;
    using Payload = Payloads::ActorMaxValueChanges;

    RequestActorMaxValueChanges()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const RequestActorMaxValueChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
