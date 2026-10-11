#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyDeathStateChange final : ServerMessage, Payloads::DeathStateChange
{
    static constexpr ServerOpcode Opcode = kNotifyDeathStateChange;
    using Payload = Payloads::DeathStateChange;

    NotifyDeathStateChange()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyDeathStateChange& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
