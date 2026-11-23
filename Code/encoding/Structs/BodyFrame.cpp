#include <Structs/BodyFrame.h>
#include <Wire/Serialization.h>

#include <algorithm>
#include <cmath>

using Wire::Serialization;

namespace
{
// Positions in 1/32 unit over 24 bits (±262144: every worldspace), velocities in 1/4 unit/s and angular ones in
// 1/512 rad/s over 16 bits, a rotation's three smallest components over 10 bits each.
constexpr float kPositionScale = 32.f;
constexpr uint32_t kPositionBits = 24;
constexpr float kVelocityScale = 4.f;
constexpr float kAngularScale = 512.f;
constexpr uint32_t kVelocityBits = 16;
constexpr uint32_t kRotationBits = 10;
constexpr float kRotationRange = 0.70710678f; // a quaternion's components but its largest lie within ±1/√2

uint64_t ToBits(float aValue, float aScale, uint32_t aBits) noexcept
{
    const int64_t cHalf = int64_t{1} << (aBits - 1);
    const int64_t cValue = std::clamp<int64_t>(std::llround(aValue * aScale), -cHalf, cHalf - 1);
    return static_cast<uint64_t>(cValue + cHalf);
}

float FromBits(uint64_t aBits, float aScale, uint32_t aCount) noexcept
{
    const int64_t cHalf = int64_t{1} << (aCount - 1);
    return static_cast<float>(static_cast<int64_t>(aBits) - cHalf) / aScale;
}

void WriteVector(Wire::Buffer::Writer& aWriter, const glm::vec3& acValue, float aScale, uint32_t aBits) noexcept
{
    for (int i = 0; i < 3; ++i)
        aWriter.WriteBits(ToBits(acValue[i], aScale, aBits), aBits);
}

glm::vec3 ReadVector(Wire::Buffer::Reader& aReader, float aScale, uint32_t aBits) noexcept
{
    glm::vec3 value{};
    for (int i = 0; i < 3; ++i)
    {
        uint64_t bits = 0;
        aReader.ReadBits(bits, aBits);
        value[i] = FromBits(bits, aScale, aBits);
    }
    return value;
}

// Smallest three: the largest component's index, then the others, the largest made positive.
uint32_t PackRotation(glm::quat aRotation) noexcept
{
    aRotation = glm::normalize(aRotation);
    const float cComponents[4] = {aRotation.x, aRotation.y, aRotation.z, aRotation.w};
    int largest = 0;
    for (int i = 1; i < 4; ++i)
        if (std::abs(cComponents[i]) > std::abs(cComponents[largest]))
            largest = i;
    const float cSign = cComponents[largest] < 0.f ? -1.f : 1.f;
    const uint32_t cMax = (1u << kRotationBits) - 1;
    uint32_t packed = static_cast<uint32_t>(largest);
    for (int i = 0, slot = 0; i < 4; ++i)
    {
        if (i == largest)
            continue;
        const float cNormalized = (cSign * cComponents[i] / kRotationRange + 1.f) * 0.5f;
        const uint32_t cValue = static_cast<uint32_t>(std::lround(std::clamp(cNormalized, 0.f, 1.f) * cMax));
        packed |= cValue << (2 + kRotationBits * slot++);
    }
    return packed;
}

glm::quat UnpackRotation(uint32_t aPacked) noexcept
{
    const int cLargest = static_cast<int>(aPacked & 3);
    const uint32_t cMax = (1u << kRotationBits) - 1;
    float components[4]{};
    float sum = 0.f;
    for (int i = 0, slot = 0; i < 4; ++i)
    {
        if (i == cLargest)
            continue;
        const uint32_t cValue = (aPacked >> (2 + kRotationBits * slot++)) & cMax;
        components[i] = (static_cast<float>(cValue) / cMax * 2.f - 1.f) * kRotationRange;
        sum += components[i] * components[i];
    }
    components[cLargest] = std::sqrt(std::max(0.f, 1.f - sum));
    return glm::normalize(glm::quat(components[3], components[0], components[1], components[2]));
}

void WriteKey(Wire::Buffer::Writer& aWriter, const BodyKey& acKey) noexcept
{
    acKey.Object.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, acKey.Actor);
    aWriter.WriteBits(acKey.Index, 8);
}

void ReadKey(Wire::Buffer::Reader& aReader, BodyKey& aKey) noexcept
{
    aKey.Object.Deserialize(aReader);
    aKey.Actor = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    uint64_t index = 0;
    aReader.ReadBits(index, 8);
    aKey.Index = static_cast<uint8_t>(index);
}
} // namespace

void BodyFrame::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    WriteKey(aWriter, Key);
    WriteVector(aWriter, Position, kPositionScale, kPositionBits);
    aWriter.WriteBits(PackRotation(Rotation), 32);
    WriteVector(aWriter, LinearVelocity, kVelocityScale, kVelocityBits);
    WriteVector(aWriter, AngularVelocity, kAngularScale, kVelocityBits);
    Serialization::WriteBool(aWriter, Gravity);
    if (Key.Actor)
        Serialization::WriteBool(aWriter, Knocked);
}

void BodyFrame::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    ReadKey(aReader, Key);
    Position = ReadVector(aReader, kPositionScale, kPositionBits);
    uint64_t rotation = 0;
    aReader.ReadBits(rotation, 32);
    Rotation = UnpackRotation(static_cast<uint32_t>(rotation));
    LinearVelocity = ReadVector(aReader, kVelocityScale, kVelocityBits);
    AngularVelocity = ReadVector(aReader, kAngularScale, kVelocityBits);
    Gravity = Serialization::ReadBool(aReader);
    Knocked = Key.Actor && Serialization::ReadBool(aReader);
}

BodyFrame BodyFrame::Quantized() const noexcept
{
    BodyFrame frame = *this;
    for (int i = 0; i < 3; ++i)
    {
        frame.Position[i] = FromBits(ToBits(Position[i], kPositionScale, kPositionBits), kPositionScale, kPositionBits);
        frame.LinearVelocity[i] = FromBits(ToBits(LinearVelocity[i], kVelocityScale, kVelocityBits), kVelocityScale, kVelocityBits);
        frame.AngularVelocity[i] = FromBits(ToBits(AngularVelocity[i], kAngularScale, kVelocityBits), kAngularScale, kVelocityBits);
    }
    frame.Rotation = UnpackRotation(PackRotation(Rotation));
    return frame;
}

void BodyPose::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    WriteKey(aWriter, Key);
    for (int i = 0; i < 3; ++i)
        Serialization::WriteFloat(aWriter, Position[i]);
    Serialization::WriteFloat(aWriter, Rotation.x);
    Serialization::WriteFloat(aWriter, Rotation.y);
    Serialization::WriteFloat(aWriter, Rotation.z);
    Serialization::WriteFloat(aWriter, Rotation.w);
    if (Key.Actor)
        Serialization::WriteBool(aWriter, Knocked);
}

void BodyPose::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    ReadKey(aReader, Key);
    for (int i = 0; i < 3; ++i)
        Position[i] = Serialization::ReadFloat(aReader);
    Rotation.x = Serialization::ReadFloat(aReader);
    Rotation.y = Serialization::ReadFloat(aReader);
    Rotation.z = Serialization::ReadFloat(aReader);
    Rotation.w = Serialization::ReadFloat(aReader);
    Knocked = Key.Actor && Serialization::ReadBool(aReader);
}

void BodyState::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    WriteKey(aWriter, Frame.Key);
    aWriter.WriteBits(static_cast<uint64_t>(Author), 2);
    aWriter.WriteBits(AuthSeq, 16);
    aWriter.WriteBits(HoldSeq, 16);
    Serialization::WriteBool(aWriter, Held);
    Serialization::WriteBool(aWriter, AtRest);
    if (AtRest)
        Serialization::WriteBool(aWriter, Placed);
    if (AtRest)
    {
        for (int i = 0; i < 3; ++i)
            Serialization::WriteFloat(aWriter, Frame.Position[i]);
        Serialization::WriteFloat(aWriter, Frame.Rotation.x);
        Serialization::WriteFloat(aWriter, Frame.Rotation.y);
        Serialization::WriteFloat(aWriter, Frame.Rotation.z);
        Serialization::WriteFloat(aWriter, Frame.Rotation.w);
    }
    else
    {
        WriteVector(aWriter, Frame.Position, kPositionScale, kPositionBits);
        aWriter.WriteBits(PackRotation(Frame.Rotation), 32);
        WriteVector(aWriter, Frame.LinearVelocity, kVelocityScale, kVelocityBits);
        WriteVector(aWriter, Frame.AngularVelocity, kAngularScale, kVelocityBits);
        Serialization::WriteBool(aWriter, Frame.Gravity);
    }
    if (Frame.Key.Actor)
        Serialization::WriteBool(aWriter, Frame.Knocked);
}

void BodyState::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    *this = {};
    ReadKey(aReader, Frame.Key);
    uint64_t bits = 0;
    aReader.ReadBits(bits, 2);
    Author = static_cast<BodyAuthor>(std::min<uint64_t>(bits, 2));
    aReader.ReadBits(bits, 16);
    AuthSeq = static_cast<uint16_t>(bits);
    aReader.ReadBits(bits, 16);
    HoldSeq = static_cast<uint16_t>(bits);
    Held = Serialization::ReadBool(aReader);
    AtRest = Serialization::ReadBool(aReader);
    Placed = AtRest && Serialization::ReadBool(aReader);
    if (AtRest)
    {
        for (int i = 0; i < 3; ++i)
            Frame.Position[i] = Serialization::ReadFloat(aReader);
        Frame.Rotation.x = Serialization::ReadFloat(aReader);
        Frame.Rotation.y = Serialization::ReadFloat(aReader);
        Frame.Rotation.z = Serialization::ReadFloat(aReader);
        Frame.Rotation.w = Serialization::ReadFloat(aReader);
    }
    else
    {
        Frame.Position = ReadVector(aReader, kPositionScale, kPositionBits);
        aReader.ReadBits(bits, 32);
        Frame.Rotation = UnpackRotation(static_cast<uint32_t>(bits));
        Frame.LinearVelocity = ReadVector(aReader, kVelocityScale, kVelocityBits);
        Frame.AngularVelocity = ReadVector(aReader, kAngularScale, kVelocityBits);
        Frame.Gravity = Serialization::ReadBool(aReader);
    }
    Frame.Knocked = Frame.Key.Actor && Serialization::ReadBool(aReader);
}

BodyState BodyState::Quantized() const noexcept
{
    BodyState state = *this;
    if (AtRest)
    {
        state.Frame.LinearVelocity = state.Frame.AngularVelocity = {};
        state.Frame.Gravity = false;
    }
    else
    {
        state.Frame = Frame.Quantized();
        state.Placed = false;
    }
    if (!state.Frame.Key.Actor)
        state.Frame.Knocked = false;
    return state;
}
