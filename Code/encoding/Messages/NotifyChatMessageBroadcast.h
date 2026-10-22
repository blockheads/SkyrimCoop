#pragma once

#include "Message.h"

struct NotifyChatMessageBroadcast final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyChatMessageBroadcast;

    NotifyChatMessageBroadcast()
        : NotifyMessage(Opcode)
    {
    }

    virtual ~NotifyChatMessageBroadcast() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyChatMessageBroadcast& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && MessageType == acRhs.MessageType && PlayerName == acRhs.PlayerName && ChatMessage == acRhs.ChatMessage; }

    ChatMessageType MessageType;
    std::string PlayerName;
    std::string ChatMessage;
};
