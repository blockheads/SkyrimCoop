#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyActorMaxValueChanges final : NotifyMessage, Payloads::ActorMaxValueChanges
{
    static constexpr NotifyOpcode Opcode = kNotifyActorMaxValueChanges;
    using Payload = Payloads::ActorMaxValueChanges;

    NotifyActorMaxValueChanges()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyActorMaxValueChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
