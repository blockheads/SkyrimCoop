#include <Messages/NotifyChatMessageBroadcast.h>

void NotifyChatMessageBroadcast::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<uint64_t>(MessageType));
    Serialization::WriteString(aWriter, PlayerName);
    Serialization::WriteString(aWriter, ChatMessage);
}

void NotifyChatMessageBroadcast::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    MessageType = static_cast<ChatMessageType>(Serialization::ReadVarInt(aReader));
    PlayerName = Serialization::ReadString(aReader);
    ChatMessage = Serialization::ReadString(aReader);
}
