#include <Messages/ActivateRequest.h>

void ActivateRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ActivatorId);
    Serialization::WriteVarInt(aWriter, NpcId);
}

void ActivateRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Id.Deserialize(aReader);
    CellId.Deserialize(aReader);
    ActivatorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    NpcId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
