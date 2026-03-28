#pragma once
#include <entt/entt.hpp>

// InterpolationSystem: Pure math system that smooths remote actor positions.
// No game API calls -- operates entirely on InterpolationComponent data.
// The interpolated positions are then sent to the DLL via CMD_SET_POSITION by CharacterService.
class InterpolationSystem {
public:
    // Update all entities with InterpolationComponent: lerp toward target
    static void Update(entt::registry& aRegistry, float aDeltaTime);

private:
    // Interpolation speed: higher = faster catch-up to target
    static constexpr float kLerpSpeed = 8.0f;
};
