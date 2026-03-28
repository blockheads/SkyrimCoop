#include "InterpolationSystem.h"

#include <glm/glm.hpp>
#include <glm/common.hpp>

#include "../Services/CharacterService.h" // for InterpolationComponent

void InterpolationSystem::Update(entt::registry& aRegistry, float aDeltaTime)
{
    // Compute interpolation factor: clamped to [0, 1]
    float alpha = glm::clamp(aDeltaTime * kLerpSpeed, 0.0f, 1.0f);

    auto view = aRegistry.view<InterpolationComponent>();

    for (auto entity : view) {
        auto& comp = view.get<InterpolationComponent>(entity);

        if (!comp.initialized)
            continue;

        // Linearly interpolate position toward target
        comp.currentPos = glm::mix(comp.currentPos, comp.targetPos, alpha);

        // Linearly interpolate rotation toward target
        comp.currentRot = glm::mix(comp.currentRot, comp.targetRot, alpha);
    }
}
