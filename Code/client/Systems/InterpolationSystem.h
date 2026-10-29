#pragma once

#include <Components/InterpolationComponent.h>

struct World;

/**
 * @brief Manages interpolation of movement and animations.
 */
struct InterpolationSystem
{
    // Where a remote actor is at aTick between two snapshots: the position linearly, each rotation axis the
    // short way round; past the second, carried on its way for up to aAheadMs (not lying), then held. False if
    // the result is not finite.
    static bool Interpolate(const InterpolationComponent::TimePoint& acFirst, const InterpolationComponent::TimePoint& acSecond, uint64_t aTick, glm::vec3& aPosition, glm::vec3& aRotation, uint64_t aAheadMs = 0) noexcept;
    static void AddPoint(InterpolationComponent& aInterpolationComponent, const InterpolationComponent::TimePoint& acPoint) noexcept;
    // A copy's owner time shown this frame (CopyPlayback on the game's CopyClock).
    static uint64_t PlaybackTick(World& aWorld, entt::entity aEntity) noexcept;
    static InterpolationComponent& Setup(World& aWorld, entt::entity aEntity) noexcept;
    static void Clean(World& aWorld, entt::entity aEntity) noexcept;
};
