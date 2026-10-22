#include <Messages/NotifyRemoveCharacter.h>

void NotifyRemoveCharacter::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Whereabouts.Serialize(aWriter);
}

void NotifyRemoveCharacter::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Whereabouts.Deserialize(aReader);
}
