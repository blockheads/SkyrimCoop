#include <Messages/SendChatMessageRequest.h>

void SendChatMessageRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, ChatMessage);
}

void SendChatMessageRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    ChatMessage = Serialization::ReadString(aReader);
}
