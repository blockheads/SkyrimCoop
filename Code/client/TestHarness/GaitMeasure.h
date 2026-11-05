#pragma once

#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>

namespace TestHarness
{
// One frame of an actor's walk: its place, its feet against its 3D's root (world axes), and whether its 3D was drawn.
struct GaitSample
{
    std::chrono::steady_clock::time_point Time;
    float Position[3];
    float Feet[2][3]; // left, right
    bool Drawn;       // a culled actor's skeleton keeps its last pose (docs/re/systems/actor-movement.md)
};

struct GaitReading
{
    float Travelled = 0.f; // over the drawn frames only
    float Feet = 0.f;
    double Seconds = 0.0;
    size_t Frames = 0;       // steps counted
    size_t HiddenFrames = 0; // steps left out: either end culled
};

// How far an actor went and how far its feet moved against its body since aFrom, counting only steps between two
// drawn frames: what a player can see. More than cTeleport in one frame is a teleport, not a step.
inline GaitReading MeasureGait(const std::deque<GaitSample>& acSamples, std::chrono::steady_clock::time_point aFrom) noexcept
{
    constexpr float cTeleport = 200.f;
    const auto cDistance = [](const float* acA, const float* acB, int aAxes) {
        float sum = 0.f;
        for (int i = 0; i < aAxes; ++i)
            sum += (acA[i] - acB[i]) * (acA[i] - acB[i]);
        return std::sqrt(sum);
    };
    GaitReading reading;
    const GaitSample* pFirst = nullptr;
    const GaitSample* pLast = nullptr;
    for (const GaitSample& cSample : acSamples)
    {
        if (cSample.Time < aFrom)
            continue;
        if (!pFirst)
            pFirst = &cSample;
        if (pLast)
        {
            const float cStep = cDistance(cSample.Position, pLast->Position, 2);
            if (cStep < cTeleport && (!cSample.Drawn || !pLast->Drawn))
                ++reading.HiddenFrames;
            else if (cStep < cTeleport)
            {
                reading.Travelled += cStep;
                reading.Feet += (cDistance(cSample.Feet[0], pLast->Feet[0], 3) + cDistance(cSample.Feet[1], pLast->Feet[1], 3)) / 2.f;
                ++reading.Frames;
            }
        }
        pLast = &cSample;
    }
    if (pFirst && pLast)
        reading.Seconds = std::chrono::duration<double>(pLast->Time - pFirst->Time).count();
    return reading;
}
} // namespace TestHarness
