#include <Messages/RequestRemoveWaypoint.h>

void RequestRemoveWaypoint::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
}

void RequestRemoveWaypoint::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
}
