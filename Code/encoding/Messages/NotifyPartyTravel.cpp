#include <Messages/NotifyPartyTravel.h>

void NotifyPartyTravel::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Heading);
}

void NotifyPartyTravel::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Heading = Serialization::ReadFloat(aReader);
}
