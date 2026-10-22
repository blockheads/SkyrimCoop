#include <Messages/NotifyActivate.h>

void NotifyActivate::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ActivatorId);
    aWriter.WriteBits(PreActivationOpenState, 8);
}

void NotifyActivate::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Id.Deserialize(aReader);
    ActivatorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t preActivationOpenState = 0;
    aReader.ReadBits(preActivationOpenState, 8);
    PreActivationOpenState = preActivationOpenState & 0xFF;
}
