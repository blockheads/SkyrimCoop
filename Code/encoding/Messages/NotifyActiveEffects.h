#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyActiveEffects final : ServerMessage, Payloads::ActiveEffects
{
    static constexpr ServerOpcode Opcode = kNotifyActiveEffects;
    using Payload = Payloads::ActiveEffects;

    NotifyActiveEffects()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyActiveEffects& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
