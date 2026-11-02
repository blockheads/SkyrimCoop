#pragma once

#include <cstdint>

namespace Physics
{
// Havok's step for one frame: the frame's own time, split into equal steps when longer than aLongest
// (at most aMaxSteps). A step longer than the frame wakes resting clutter (S4, physics.md, "Havok's step").
[[nodiscard]] constexpr float HavokStep(float aFrameSeconds, float aLongest, uint32_t aMaxSteps) noexcept
{
    float step = aFrameSeconds;
    for (uint32_t steps = 2; steps <= aMaxSteps && step > aLongest; ++steps)
        step = aFrameSeconds / static_cast<float>(steps);
    return step;
}
} // namespace Physics
