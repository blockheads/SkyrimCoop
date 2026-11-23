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

void NotifyPartySummon::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Destination.SerializeRaw(aWriter);
    Serialization::WriteString(aWriter, Username);
}

void NotifyPartySummon::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    Destination.DeserializeRaw(aReader);
    Username = Serialization::ReadString(aReader);
}

void NotifyLeash::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(State), 2);
    aWriter.WriteBits(Seconds, 8);
}

void NotifyLeash::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    uint64_t state = 0;
    aReader.ReadBits(state, 2);
    State = state <= static_cast<uint64_t>(Kind::kBack) ? static_cast<Kind>(state) : Kind::kOff;
    uint64_t seconds = 0;
    aReader.ReadBits(seconds, 8);
    Seconds = static_cast<uint8_t>(seconds);
}
