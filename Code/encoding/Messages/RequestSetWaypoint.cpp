#include <Messages/RequestSetWaypoint.h>

void RequestSetWaypoint::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Position.Serialize(aWriter);
    WorldSpaceFormID.Serialize(aWriter);
}

void RequestSetWaypoint::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Position.Deserialize(aReader);
    WorldSpaceFormID.Deserialize(aReader);
}
