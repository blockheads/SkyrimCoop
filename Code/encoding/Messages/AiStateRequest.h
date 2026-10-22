#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct AiStateRequest final : RequestMessage, Payloads::AiState
{
    static constexpr RequestOpcode Opcode = kAiStateRequest;
    using Payload = Payloads::AiState;

    AiStateRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const AiStateRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
