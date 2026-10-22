#include <Messages/NotifyWorldState.h>

void NotifyWorldState::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Entities.size());

    for (const auto& entry : Entities)
    {
        Serialization::WriteVarInt(aWriter, entry.SessionId);
        entry.Position.Serialize(aWriter);
        entry.Rotation.Serialize(aWriter);
    }
}

void NotifyWorldState::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    const auto count = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Entities.resize(count);

    for (auto& entry : Entities)
    {
        entry.SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        entry.Position.Deserialize(aReader);
        entry.Rotation.Deserialize(aReader);
    }
}
