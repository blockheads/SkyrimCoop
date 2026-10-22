#pragma once

#include "Message.h"
#include "Payloads.h"

struct DialogueRequest final : RequestMessage, Payloads::Dialogue
{
    static constexpr RequestOpcode Opcode = kDialogueRequest;
    using Payload = Payloads::Dialogue;

    DialogueRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const DialogueRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
