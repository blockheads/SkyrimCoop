#include <Messages/StoryEventRequest.h>

void StoryEventRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Event.Type, 8);
    Event.Workbench.Serialize(aWriter);
    Event.OldLocation.Serialize(aWriter);
    Event.Location.Serialize(aWriter);
    Event.Created.Serialize(aWriter);
}

void StoryEventRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    uint64_t type = 0;
    aReader.ReadBits(type, 8);
    Event.Type = static_cast<StoryEvent::Kind>(type);
    Event.Workbench.Deserialize(aReader);
    Event.OldLocation.Deserialize(aReader);
    Event.Location.Deserialize(aReader);
    Event.Created.Deserialize(aReader);
}
