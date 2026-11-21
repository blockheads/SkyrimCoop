#include <Messages/NotifyActivate.h>

void NotifyActivate::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ActivatorId);
    Serialization::WriteVarInt(aWriter, NpcId);
}

void NotifyActivate::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Id.Deserialize(aReader);
    ActivatorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    NpcId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
