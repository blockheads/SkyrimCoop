#pragma once

#include <Structs/BodyFrame.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <vector>

// Which moving bodies' states a guest gets (docs/design/physics-authority-plan.md, piece 3): those near its copy.
// Resting ones go to every guest. No engine: UnitTests.
namespace BodyRelevance
{
// Around a guest's copy: one exterior cell.
constexpr float kGuestRadius = 4096.f;

[[nodiscard]] inline bool IsNear(const glm::vec3& acBody, const glm::vec3& acGuest) noexcept
{
    return glm::distance(acBody, acGuest) <= kGuestRadius;
}
} // namespace BodyRelevance
