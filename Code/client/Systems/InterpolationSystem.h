#pragma once

struct World;

/**
 * @brief Manages interpolation of movement and animations.
 */
struct InterpolationSystem
{
    // Where a remote actor is at aTick between two snapshots: the position linearly, each rotation axis the
    // short way round. False if the result is not finite.
    static bool Interpolate(const InterpolationComponent::TimePoint& acFirst, const InterpolationComponent::TimePoint& acSecond, uint64_t aTick, glm::vec3& aPosition, glm::vec3& aRotation) noexcept;
    static void AddPoint(InterpolationComponent& aInterpolationComponent, const InterpolationComponent::TimePoint& acPoint) noexcept;
    static InterpolationComponent& Setup(World& aWorld, entt::entity aEntity) noexcept;
    static void Clean(World& aWorld, entt::entity aEntity) noexcept;
};
