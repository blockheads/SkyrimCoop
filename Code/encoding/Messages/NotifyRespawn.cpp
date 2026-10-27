#include <Messages/NotifyRespawn.h>

void NotifyRespawn::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorId);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    if (AppearanceBuffer.empty())
        return;
    Serialization::WriteVarInt(aWriter, ChangeFlags);
    FaceTints.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Weight);
}

void NotifyRespawn::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    ActorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    AppearanceBuffer = Serialization::ReadString(aReader);
    ChangeFlags = 0;
    FaceTints = {};
    Weight = 50.f;
    if (AppearanceBuffer.empty())
        return;
    ChangeFlags = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    FaceTints.Deserialize(aReader);
    Weight = Serialization::ReadFloat(aReader);
}
