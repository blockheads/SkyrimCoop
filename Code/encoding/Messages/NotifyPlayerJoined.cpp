#include <Messages/NotifyPlayerJoined.h>
#include <Wire/Serialization.h>

void NotifyPlayerJoined::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteString(aWriter, Username);
    Serialization::WriteVarInt(aWriter, Level);
    Serialization::WriteBool(aWriter, IsHost);
    Serialization::WriteBool(aWriter, Arrived);
}

void NotifyPlayerJoined::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Username = Serialization::ReadString(aReader);
    Level = Serialization::ReadVarInt(aReader) & 0xFFFF;
    IsHost = Serialization::ReadBool(aReader);
    Arrived = Serialization::ReadBool(aReader);
}
