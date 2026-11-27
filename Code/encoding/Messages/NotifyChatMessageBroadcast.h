#pragma once

#include "Message.h"
#include <ChatMessageTypes.h>
#include <Wire/Buffer.h>

struct NotifyChatMessageBroadcast final : MessageOf<NotifyChatMessageBroadcast, kNotifyChatMessageBroadcast>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(MessageType, PlayerName, ChatMessage);
    }
    bool operator==(const NotifyChatMessageBroadcast&) const noexcept = default;

    ChatMessageType MessageType = kPlayerChat;
    std::string PlayerName;
    std::string ChatMessage;
};
