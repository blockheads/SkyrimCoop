#include <Messages/AuthenticationRequest.h>

void AuthenticationRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Game.Serialize(aWriter);
    Serialization::WriteString(aWriter, Version);
    UserMods.Serialize(aWriter);
    Serialization::WriteString(aWriter, Username);
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Level);
    PlayerTime.Serialize(aWriter);
    Serialization::WriteString(aWriter, PlayerKey);
    Serialization::WriteBool(aWriter, WantsWorld);
    Serialization::WriteBool(aWriter, HasCharacter);
    Serialization::WriteVarInt(aWriter, CachedSaveBlocks.size());
    for (const auto& hash : CachedSaveBlocks)
        Serialization::WriteString(aWriter, hash);
}

void AuthenticationRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Game.Deserialize(aReader);
    Version = Serialization::ReadString(aReader);
    UserMods.Deserialize(aReader);
    Username = Serialization::ReadString(aReader);
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Level = Serialization::ReadVarInt(aReader) & 0xFFFF;
    PlayerTime.Deserialize(aReader);
    PlayerKey = Serialization::ReadString(aReader);
    WantsWorld = Serialization::ReadBool(aReader);
    HasCharacter = Serialization::ReadBool(aReader);
    CachedSaveBlocks.clear();
    const auto cCount = Serialization::ReadVarInt(aReader);
    if (cCount > 16384)
        return;
    for (uint64_t i = 0; i < cCount; ++i)
    {
        auto hash = Serialization::ReadString(aReader);
        if (hash.size() != 32)
        {
            CachedSaveBlocks.clear();
            return;
        }
        CachedSaveBlocks.push_back(std::move(hash));
    }
}
