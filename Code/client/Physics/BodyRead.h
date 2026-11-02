#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

// A body this near a pose is there: a settle in place, not a move of its own.
constexpr float kBodySameDistance = 0.5f; // units
constexpr float kBodySameAngle = 0.005f;  // radians

// A rigid body as the guests get it, in game units: where (its Havok transform), how it moves, and whether it is
// simulated (dynamic) and awake (its island active).
struct BodyRead
{
    glm::vec3 Position{};
    glm::quat Rotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 LinearVelocity{};
    glm::vec3 AngularVelocity{};
    bool Dynamic{};
    bool Active{};
};
