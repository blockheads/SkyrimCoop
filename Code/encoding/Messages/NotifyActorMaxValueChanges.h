#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyActorMaxValueChanges final : ServerMessage, Payloads::ActorMaxValueChanges
{
    static constexpr ServerOpcode Opcode = kNotifyActorMaxValueChanges;
    using Payload = Payloads::ActorMaxValueChanges;

    NotifyActorMaxValueChanges()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyActorMaxValueChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
