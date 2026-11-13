#include <Structs/Movement.h>
#include <Wire/Serialization.h>

#include <algorithm>
#include <cmath>

using Wire::Serialization;

namespace
{
// Direction (radians, the middle process's) in 1/4096 over 16 bits: ±8, a 0.00025 rad step.
constexpr float kDirectionScale = 4096.f;
constexpr uint32_t kDirectionBits = 16;
} // namespace

bool Movement::operator==(const Movement& acRhs) const noexcept
{
    return CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && Rotation == acRhs.Rotation && Variables == acRhs.Variables && Direction == acRhs.Direction &&
           Teleported == acRhs.Teleported && Lying == acRhs.Lying && Knocked == acRhs.Knocked &&
           OnGround == acRhs.OnGround && CapsuleCollides == acRhs.CapsuleCollides && BodiesLayer == acRhs.BodiesLayer &&
           GraphsStepPhysics == acRhs.GraphsStepPhysics;
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
    // Against zeros of its sizes: only the variables set travel (ApplyDiff fills the rest with zeros).
    AnimationVariables zeros;
    zeros.Booleans.resize(Variables.Booleans.size());
    zeros.Integers.resize(Variables.Integers.size());
    zeros.Floats.resize(Variables.Floats.size());
    Variables.GenerateDiff(zeros, aWriter);
    constexpr int64_t cHalf = int64_t{1} << (kDirectionBits - 1);
    const int64_t cDirection = std::isfinite(Direction) ? std::clamp<int64_t>(std::llround(Direction * kDirectionScale), -cHalf, cHalf - 1) : 0;
    aWriter.WriteBits(static_cast<uint64_t>(cDirection + cHalf), kDirectionBits);
    Serialization::WriteBool(aWriter, Teleported);
    Serialization::WriteBool(aWriter, Lying);
    Serialization::WriteBool(aWriter, Knocked);
    Serialization::WriteBool(aWriter, OnGround);
    Serialization::WriteBool(aWriter, CapsuleCollides);
    aWriter.WriteBits(BodiesLayer & 0x7F, 7); // a collision layer: 7 bits
    Serialization::WriteBool(aWriter, GraphsStepPhysics);
}

void Movement::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);
    Variables = AnimationVariables{};
    Variables.ApplyDiff(aReader);

    uint64_t direction = 0;
    aReader.ReadBits(direction, kDirectionBits);
    Direction = static_cast<float>(static_cast<int64_t>(direction) - (int64_t{1} << (kDirectionBits - 1))) / kDirectionScale;
    Teleported = Serialization::ReadBool(aReader);
    Lying = Serialization::ReadBool(aReader);
    Knocked = Serialization::ReadBool(aReader);
    OnGround = Serialization::ReadBool(aReader);
    CapsuleCollides = Serialization::ReadBool(aReader);
    uint64_t layer = 0;
    aReader.ReadBits(layer, 7);
    BodiesLayer = static_cast<uint8_t>(layer);
    GraphsStepPhysics = Serialization::ReadBool(aReader);
}
