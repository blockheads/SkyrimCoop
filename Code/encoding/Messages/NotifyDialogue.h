#pragma once

#include "Message.h"
#include "Payloads.h"

struct NotifyDialogue final : ServerMessage, Payloads::Dialogue
{
    static constexpr ServerOpcode Opcode = kNotifyDialogue;
    using Payload = Payloads::Dialogue;

    NotifyDialogue()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyDialogue& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
