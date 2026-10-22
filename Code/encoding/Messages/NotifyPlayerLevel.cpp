#include <Messages/NotifyPlayerLevel.h>

void NotifyPlayerLevel::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteVarInt(aWriter, NewLevel);
}

void NotifyPlayerLevel::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    NewLevel = Serialization::ReadVarInt(aReader) & 0xFFFF;
}
