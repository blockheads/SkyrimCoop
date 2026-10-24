#include <Messages/AuthenticationResponse.h>

void AuthenticationResponse::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<uint32_t>(Type));
    Serialization::WriteString(aWriter, Version);
    Game.Serialize(aWriter);
    UserMods.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Difficulty);
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteVarInt(aWriter, CharacterId);
}

void AuthenticationResponse::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Type = static_cast<ResponseType>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
    Version = Serialization::ReadString(aReader);
    Game.Deserialize(aReader);
    UserMods.Deserialize(aReader);
    Difficulty = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CharacterId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
