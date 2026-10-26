#pragma once

#include <Structs/BodyFrame.h>

#include <glm/vec3.hpp>

#include <optional>
#include <vector>

// Which awake bodies' frames a guest gets (docs/design/physics-sync.md, "Near each guest"): those near its copy.
// Rests go to every guest. No engine: ClientTests.
namespace BodyRelevance
{
// Around a guest's copy: one exterior cell.
constexpr float kGuestRadius = 4096.f;

// acAwake's frames within kGuestRadius of the guest's copy; all of them while the host's game shows no copy.
[[nodiscard]] std::vector<BodyFrame> Near(const std::vector<BodyFrame>& acAwake, const std::optional<glm::vec3>& acGuest) noexcept;
} // namespace BodyRelevance
