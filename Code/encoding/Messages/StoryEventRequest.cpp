#include <Messages/StoryEventRequest.h>

void StoryEventRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Event.Type, 32);
    Serialization::WriteVarInt(aWriter, Event.Members.size());
    for (const StoryEvent::Member& cMember : Event.Members)
    {
        aWriter.WriteBits(cMember.Type, 8);
        if (cMember.Type == StoryEvent::Member::kReference || cMember.Type == StoryEvent::Member::kForm)
            cMember.Form.Serialize(aWriter);
        else if (cMember.Type == StoryEvent::Member::kInteger)
            Serialization::WriteVarInt(aWriter, cMember.Value);
    }
}

void StoryEventRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    uint64_t value = 0;
    aReader.ReadBits(value, 32);
    Event.Type = static_cast<uint32_t>(value);
    Event.Members.clear();
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    for (uint64_t i = 0; i < cCount && i < StoryEvent::kMaxMembers; ++i)
    {
        StoryEvent::Member& member = Event.Members.emplace_back();
        aReader.ReadBits(value, 8);
        member.Type = static_cast<StoryEvent::Member::Kind>(value);
        if (member.Type == StoryEvent::Member::kReference || member.Type == StoryEvent::Member::kForm)
            member.Form.Deserialize(aReader);
        else if (member.Type == StoryEvent::Member::kInteger)
            member.Value = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    }
}
