#include <Messages/NotifyPlayerJoined.h>
#include <TiltedCore/Serialization.hpp>

void NotifyPlayerJoined::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteString(aWriter, Username);
    Serialization::WriteVarInt(aWriter, Level);
    Serialization::WriteBool(aWriter, IsHost);
}

void NotifyPlayerJoined::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Username = Serialization::ReadString(aReader);
    Level = Serialization::ReadVarInt(aReader) & 0xFFFF;
    IsHost = Serialization::ReadBool(aReader);
}
