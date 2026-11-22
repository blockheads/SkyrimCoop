#include <Structs/RidePart.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

void RidePart::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Mount);
}

void RidePart::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    Tick = Serialization::ReadVarInt(aReader);
    Mount = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
