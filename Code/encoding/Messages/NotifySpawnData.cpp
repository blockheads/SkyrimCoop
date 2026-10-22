#include <Messages/NotifySpawnData.h>

void NotifySpawnData::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    NewActorData.Serialize(aWriter);
}

void NotifySpawnData::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    NewActorData.Deserialize(aReader);
}
