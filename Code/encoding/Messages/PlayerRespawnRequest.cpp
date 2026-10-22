#include <Messages/PlayerRespawnRequest.h>

void PlayerRespawnRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
}

void PlayerRespawnRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
}
