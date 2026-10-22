#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyActiveEffects final : NotifyMessage, Payloads::ActiveEffects
{
    static constexpr NotifyOpcode Opcode = kNotifyActiveEffects;
    using Payload = Payloads::ActiveEffects;

    NotifyActiveEffects()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyActiveEffects& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
