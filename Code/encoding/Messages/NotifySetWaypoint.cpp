#include <Messages/NotifySetWaypoint.h>

void NotifySetWaypoint::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Position.Serialize(aWriter);
    WorldSpaceFormID.Serialize(aWriter);
}

void NotifySetWaypoint::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Position.Deserialize(aReader);
    WorldSpaceFormID.Deserialize(aReader);
}
