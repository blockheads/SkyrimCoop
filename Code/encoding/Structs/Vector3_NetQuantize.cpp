#include <Structs/Vector3_NetQuantize.h>
#include <Wire/Serialization.h>

#include <algorithm>
#include <cmath>

using Wire::Serialization;

bool Vector3_NetQuantize::operator==(const Vector3_NetQuantize& acRhs) const noexcept
{
    return Pack() == acRhs.Pack();
}

bool Vector3_NetQuantize::operator!=(const Vector3_NetQuantize& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

Vector3_NetQuantize& Vector3_NetQuantize::operator=(const glm::vec3& acRhs) noexcept
{
    glm::vec3::operator=(acRhs);
    return *this;
}

void Vector3_NetQuantize::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Pack(), 64);
}

void Vector3_NetQuantize::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    uint64_t data;
    aReader.ReadBits(data, 64);

    Unpack(data);
}

namespace
{
// Quarter units, rounded, in one word: x and y over 22 bits (±524288), z over 20 (±131072). Whole units made a walking
// copy step unevenly (docs/design/copy-clock.md, "Measuring smoothness").
constexpr float kScale = 4.f;
constexpr uint32_t kBits[3] = {22, 22, 20};

uint64_t ToBits(float aValue, uint32_t aBits) noexcept
{
    const int64_t cHalf = int64_t{1} << (aBits - 1);
    const int64_t cValue = std::isfinite(aValue) ? std::clamp<int64_t>(std::llround(aValue * kScale), -cHalf, cHalf - 1) : 0;
    return static_cast<uint64_t>(cValue + cHalf);
}
} // namespace

void Vector3_NetQuantize::Unpack(uint64_t aValue) noexcept
{
    for (int axis = 0, shift = 0; axis < 3; shift += kBits[axis++])
    {
        const int64_t cHalf = int64_t{1} << (kBits[axis] - 1);
        const auto cBits = static_cast<int64_t>((aValue >> shift) & ((uint64_t{1} << kBits[axis]) - 1));
        (*this)[axis] = static_cast<float>(cBits - cHalf) / kScale;
    }
}

uint64_t Vector3_NetQuantize::Pack() const noexcept
{
    uint64_t data = 0;
    for (int axis = 0, shift = 0; axis < 3; shift += kBits[axis++])
        data |= ToBits((*this)[axis], kBits[axis]) << shift;
    return data;
}
