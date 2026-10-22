#include <Messages/NotifyPlayerRespawn.h>

void NotifyPlayerRespawn::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, GoldLost);
}

void NotifyPlayerRespawn::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    GoldLost = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
