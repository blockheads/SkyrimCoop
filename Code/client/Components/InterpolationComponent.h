#pragma once

#include <Structs/AnimationVariables.h>
#include <Systems/CopyEase.h>

struct InterpolationComponent
{
    struct TimePoint
    {
        uint64_t Tick{};
        glm::vec3 Position{};
        glm::vec3 Rotation{};
        AnimationVariables Variables{};
        float Direction{};
        bool Lying{}; // its owner lying in its ragdoll (Movement::Lying)

        TimePoint() = default;
        TimePoint(const TimePoint&) = default;
        TimePoint& operator=(const TimePoint&) = default;
    };

    std::list<TimePoint> TimePoints;
    glm::vec3 Position;
    CopyEase Ease;
};
