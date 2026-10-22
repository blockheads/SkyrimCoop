#include <Structs/Rotator2_NetQuantize.h>
#include <cmath>
#include <numbers>
#include <Wire/Serialization.h>

using Wire::Serialization;

constexpr float cScalingFactory = float(0xFFFF) / (2.0f * std::numbers::pi_v<float>);
constexpr float cReverseScalingFactory = 1.0f / cScalingFactory;

bool Rotator2_NetQuantize::operator==(const Rotator2_NetQuantize& acRhs) const noexcept
{
    return Pack() == acRhs.Pack();
}

bool Rotator2_NetQuantize::operator!=(const Rotator2_NetQuantize& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Rotator2_NetQuantize::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Pack(), 32);
}

void Rotator2_NetQuantize::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    uint64_t data = 0;
    aReader.ReadBits(data, 32);

    Unpack(data & 0xFFFFFFFF);
}

void Rotator2_NetQuantize::Unpack(uint32_t aValue) noexcept
{
    auto xValue = static_cast<float>(aValue & 0xFFFF);
    auto yValue = static_cast<float>((aValue >> 16) & 0xFFFF);

    x = xValue * cReverseScalingFactory;
    y = yValue * cReverseScalingFactory;
}

uint32_t Rotator2_NetQuantize::Pack() const noexcept
{
    uint32_t data = 0;

    auto WrapAngle = [](float angle)
    {
        angle = std::fmod(angle, 2.f * std::numbers::pi_v<float>);
        if (angle < 0.f)
        {
            angle += 2.f * std::numbers::pi_v<float>;
        }

        return angle;
    };

    uint32_t ix = static_cast<uint32_t>(WrapAngle(x) * cScalingFactory) & 0xFFFF;
    uint32_t iy = static_cast<uint32_t>(WrapAngle(y) * cScalingFactory) & 0xFFFF;

    data |= ix;
    data |= iy << 16;

    return data;
}
