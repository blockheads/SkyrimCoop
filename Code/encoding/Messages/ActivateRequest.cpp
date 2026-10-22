#include <Messages/ActivateRequest.h>

void ActivateRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ActivatorId);
    aWriter.WriteBits(PreActivationOpenState, 8);
}

void ActivateRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Id.Deserialize(aReader);
    CellId.Deserialize(aReader);
    ActivatorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t preActivationOpenState = 0;
    aReader.ReadBits(preActivationOpenState, 8);
    PreActivationOpenState = preActivationOpenState & 0xFF;
}
