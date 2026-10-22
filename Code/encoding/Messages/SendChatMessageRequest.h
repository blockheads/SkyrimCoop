#pragma once

#include "Message.h"

using TiltedPhoques::String;

struct SendChatMessageRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kSendChatMessageRequest;

    SendChatMessageRequest()
        : RequestMessage(Opcode)
    {
    }

    virtual ~SendChatMessageRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const SendChatMessageRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && MessageType == acRhs.MessageType && ChatMessage == acRhs.ChatMessage; }

    ChatMessageType MessageType;
    TiltedPhoques::String ChatMessage;
};
