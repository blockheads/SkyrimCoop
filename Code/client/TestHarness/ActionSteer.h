#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

// The pure halves of the harness's checked actions (docs/design/checked-actions.md): steering an angle by mouse
// counts, and the extremes a watch records. No engine.
namespace ActionSteer
{
// goal - at in degrees, the short way round (-180..180].
[[nodiscard]] inline float AngleError(float aGoal, float aAt) noexcept
{
    float error = std::fmod(aGoal - aAt, 360.f);
    if (error > 180.f)
        error -= 360.f;
    else if (error <= -180.f)
        error += 360.f;
    return error;
}

// One axis turned by mouse counts, closed loop: each frame it learns how far a count turned last frame (the
// player's sensitivity and inversion are unknown) and sends what reaches the goal, no faster than aMaxPerFrame.
struct Axis
{
    float DegreesPerCount = 0.02f; // a guess until measured; its sign is the axis's direction
    float LastAt = 0.f;
    int32_t LastSent = 0;
    bool Started = false;
    bool Measured = false; // until then small probes, so an unknown sensitivity never turns it suddenly

    [[nodiscard]] int32_t Step(float aGoal, float aAt, float aMaxPerFrame, float aTolerance) noexcept
    {
        if (Started && LastSent != 0)
        {
            const float cTurned = AngleError(aAt, LastAt);
            // Only a clear response teaches it (a frame that dropped the input turns nothing).
            if (std::fabs(cTurned) > 0.01f)
            {
                const float cMeasured = cTurned / static_cast<float>(LastSent);
                if (std::fabs(cMeasured) > 1e-4f && std::fabs(cMeasured) < 2.f)
                {
                    DegreesPerCount = Measured ? 0.5f * DegreesPerCount + 0.5f * cMeasured : cMeasured;
                    Measured = true;
                }
            }
        }
        Started = true;
        LastAt = aAt;
        const float cError = AngleError(aGoal, aAt);
        if (std::fabs(cError) <= aTolerance)
            return LastSent = 0;
        // Unmeasured: a probe that doubles each frame until a turn shows (the first that shows is small).
        if (!Measured)
        {
            const int32_t cProbe = std::min(LastSent ? std::abs(LastSent) * 2 : 1, 256);
            return LastSent = cError > 0.f ? cProbe : -cProbe;
        }
        const float cWanted = std::clamp(cError, -aMaxPerFrame, aMaxPerFrame);
        int32_t counts = static_cast<int32_t>(std::lround(cWanted / DegreesPerCount));
        if (counts == 0)
            counts = (cWanted / DegreesPerCount) > 0.f ? 1 : -1;
        return LastSent = counts;
    }
};

// A watched value's lowest and highest over an action, with the frame of each.
struct Extremes
{
    std::optional<float> Min, Max;
    uint64_t MinFrame = 0, MaxFrame = 0;

    void Add(float aValue, uint64_t aFrame) noexcept
    {
        if (!Min || aValue < *Min)
        {
            Min = aValue;
            MinFrame = aFrame;
        }
        if (!Max || aValue > *Max)
        {
            Max = aValue;
            MaxFrame = aFrame;
        }
    }
};
} // namespace ActionSteer
