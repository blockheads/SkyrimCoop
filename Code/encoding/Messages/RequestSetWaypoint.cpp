#include <Messages/RequestSetWaypoint.h>

void RequestSetWaypoint::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Position.Serialize(aWriter);
    WorldSpaceFormID.Serialize(aWriter);
}

void RequestSetWaypoint::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Position.Deserialize(aReader);
    WorldSpaceFormID.Deserialize(aReader);
}
