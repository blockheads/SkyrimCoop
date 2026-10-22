#include <Messages/RequestRemoveCharacter.h>

void RequestRemoveCharacter::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Whereabouts.Serialize(aWriter);
}

void RequestRemoveCharacter::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Whereabouts.Deserialize(aReader);
}
