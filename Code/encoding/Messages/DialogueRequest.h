#pragma once

#include "Message.h"
#include "Payloads.h"

struct DialogueRequest final : ClientMessage, Payloads::Dialogue
{
    static constexpr ClientOpcode Opcode = kDialogueRequest;
    using Payload = Payloads::Dialogue;

    DialogueRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const DialogueRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
