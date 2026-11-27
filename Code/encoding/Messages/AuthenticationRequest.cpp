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
    Serialization::WriteVarInt(aWriter, HeldSaves.size());
    for (const auto& save : HeldSaves)
        Serialization::WriteString(aWriter, save);
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
    HeldSaves.clear();
    const uint64_t cCount = Serialization::ReadCount(aReader, kMaxHeldSaves);
    for (uint64_t i = 0; i < cCount; ++i)
    {
        auto save = Serialization::ReadString(aReader);
        if (save.size() != 32)
        {
            HeldSaves.clear();
            return;
        }
        HeldSaves.push_back(std::move(save));
    }
}
