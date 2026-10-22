#include <Messages/RequestRemoveCharacter.h>

void RequestRemoveCharacter::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
}

void RequestRemoveCharacter::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
