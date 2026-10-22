#include <Messages/AuthenticationRequest.h>

void AuthenticationRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Game.Serialize(aWriter);
    Serialization::WriteString(aWriter, Token);
    Serialization::WriteString(aWriter, Version);
    UserMods.Serialize(aWriter);
    Serialization::WriteString(aWriter, Username);
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Level);
    PlayerTime.Serialize(aWriter);
}

void AuthenticationRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Game.Deserialize(aReader);
    Token = Serialization::ReadString(aReader);
    Version = Serialization::ReadString(aReader);
    UserMods.Deserialize(aReader);
    Username = Serialization::ReadString(aReader);
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Level = Serialization::ReadVarInt(aReader) & 0xFFFF;
    PlayerTime.Deserialize(aReader);
}
