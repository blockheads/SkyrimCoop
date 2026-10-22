#include <Messages/PlayerLevelRequest.h>

void PlayerLevelRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, NewLevel);
}

void PlayerLevelRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    NewLevel = Serialization::ReadVarInt(aReader) & 0xFFFF;
}
