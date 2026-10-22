#include <Messages/NotifyRemoveWaypoint.h>

void NotifyRemoveWaypoint::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
}

void NotifyRemoveWaypoint::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
}
