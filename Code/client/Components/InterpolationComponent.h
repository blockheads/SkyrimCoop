#pragma once

#include <Services/CopyClock.h>
#include <Structs/AnimationVariables.h>
#include <Systems/CopyCorrection.h>
#include <Systems/CopyEase.h>

#include <optional>
#include <utility>

struct InterpolationComponent
{
    struct TimePoint
    {
        uint64_t Tick{};
        glm::vec3 Position{};
        glm::vec3 Rotation{};
        AnimationVariables Variables{};
        float Direction{};
        bool Knocked{};
        bool Lying{}; // its owner lying in its ragdoll (Movement::Lying)
        bool OnGround{}; // its owner's controller on the ground (Movement::OnGround)
        bool CapsuleCollides{true}; // its owner's capsule collides (Movement::CapsuleCollides)

        TimePoint() = default;
        TimePoint(const TimePoint&) = default;
        TimePoint& operator=(const TimePoint&) = default;
    };

    std::list<TimePoint> TimePoints;
    glm::vec3 Position;
    CopyEase Ease;
    CopyCorrection Correction;
    std::optional<std::pair<TimePoint, TimePoint>> Guess; // the snapshots it was carried on from, past the newest
    CopyPlayback Playback; // its owner's time shown now: movement, actions and ragdoll alike
};
