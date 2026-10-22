#include <Messages/NotifyPlayerLeft.h>
#include <Wire/Serialization.h>

void NotifyPlayerLeft::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteString(aWriter, Username);
}

void NotifyPlayerLeft::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Username = Serialization::ReadString(aReader);
}
