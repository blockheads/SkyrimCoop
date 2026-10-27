#include <Structs/Movement.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

bool Movement::operator==(const Movement& acRhs) const noexcept
{
    return CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && Rotation == acRhs.Rotation && Variables == acRhs.Variables && Direction == acRhs.Direction &&
           Teleported == acRhs.Teleported && Lying == acRhs.Lying;
}

bool Movement::operator!=(const Movement& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Movement::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    Variables.GenerateDiff(AnimationVariables{}, aWriter);
    aWriter.WriteBits(*reinterpret_cast<const uint32_t*>(&Direction), 32);
    Serialization::WriteBool(aWriter, Teleported);
    Serialization::WriteBool(aWriter, Lying);
}

void Movement::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);
    Variables = AnimationVariables{};
    Variables.ApplyDiff(aReader);

    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 32);
    uint32_t tmp32 = tmp & 0xFFFFFFFF;
    Direction = *reinterpret_cast<float*>(&tmp32);
    Teleported = Serialization::ReadBool(aReader);
    Lying = Serialization::ReadBool(aReader);
}
