#include <Messages/NotifyRespawn.h>

void NotifyRespawn::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorId);
}

void NotifyRespawn::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    ActorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
