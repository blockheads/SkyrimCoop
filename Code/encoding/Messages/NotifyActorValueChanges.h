#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyActorValueChanges final : ServerMessage, Payloads::ActorValueChanges
{
    static constexpr ServerOpcode Opcode = kNotifyActorValueChanges;
    using Payload = Payloads::ActorValueChanges;

    NotifyActorValueChanges()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyActorValueChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
