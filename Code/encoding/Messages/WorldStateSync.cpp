#include <Messages/WorldStateSync.h>

void WorldStateSync::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Entities.size());

    for (const auto& entry : Entities)
    {
        Serialization::WriteVarInt(aWriter, entry.ServerId);
        entry.Position.Serialize(aWriter);
        entry.Rotation.Serialize(aWriter);
    }
}

void WorldStateSync::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    const auto count = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Entities.resize(count);

    for (auto& entry : Entities)
    {
        entry.ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        entry.Position.Deserialize(aReader);
        entry.Rotation.Deserialize(aReader);
    }
}
