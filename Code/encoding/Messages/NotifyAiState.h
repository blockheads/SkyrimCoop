#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Wire/Buffer.h>

struct NotifyAiState final : NotifyMessage, Payloads::AiState
{
    static constexpr NotifyOpcode Opcode = kNotifyAiState;
    using Payload = Payloads::AiState;

    NotifyAiState()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override { Payload::SerializePayload(aWriter); }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override { Payload::DeserializePayload(aReader); }

    bool operator==(const NotifyAiState& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Payload::operator==(acRhs); }
};
