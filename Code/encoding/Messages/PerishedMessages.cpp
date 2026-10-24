#include <Messages/PerishedMessages.h>
#include <Wire/Serialization.h>

void PlayerPerishedRequest::SerializeRaw(Wire::Buffer::Writer&) const noexcept
{
}

void PlayerPerishedRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
}

void NotifyPlayerPerished::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteString(aWriter, Username);
}

void NotifyPlayerPerished::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Username = Serialization::ReadString(aReader);
}
