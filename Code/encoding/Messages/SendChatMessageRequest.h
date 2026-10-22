#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct SendChatMessageRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kSendChatMessageRequest;

    SendChatMessageRequest()
        : RequestMessage(Opcode)
    {
    }

    virtual ~SendChatMessageRequest() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const SendChatMessageRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && MessageType == acRhs.MessageType && ChatMessage == acRhs.ChatMessage; }

    ChatMessageType MessageType;
    std::string ChatMessage;
};
