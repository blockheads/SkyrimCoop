#include <Structs/ActorWhereabouts.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

void ActorWhereabouts::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(Where), 8);
    if (Where != State::kPlaced)
        return;
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Heading);
}

void ActorWhereabouts::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    uint64_t where = 0;
    aReader.ReadBits(where, 8);
    *this = {};
    Where = static_cast<State>(where);
    if (Where != State::kPlaced)
        return;
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Heading = Serialization::ReadFloat(aReader);
}
