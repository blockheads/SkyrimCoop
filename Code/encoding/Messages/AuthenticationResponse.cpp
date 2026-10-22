#include <Messages/AuthenticationResponse.h>

void AuthenticationResponse::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<uint32_t>(Type));
    Serialization::WriteString(aWriter, Version);
    Game.Serialize(aWriter);
    UserMods.Serialize(aWriter);
    Settings.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, PlayerId);
}

void AuthenticationResponse::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Type = static_cast<ResponseType>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
    Version = Serialization::ReadString(aReader);
    Game.Deserialize(aReader);
    UserMods.Deserialize(aReader);
    Settings.Deserialize(aReader);
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
