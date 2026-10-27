#pragma once

#include <cstdint>
#include <optional>

namespace GameRaycast
{
struct RayHit
{
    bool Hit{};
    float Point[3]{};
    float Normal[3]{};
    float Distance{};
    uint32_t Reference{}; // the hit collision's reference, 0 for none (terrain, the cell's own)
    uint32_t Layer{};     // the hit collision's layer (COL_LAYER)
};
// No value when the physics world or pick is unavailable. Passes through the player's own collision.
std::optional<RayHit> CastRay(const float acFrom[3], const float acTo[3], uint32_t aLayer = 30, uint32_t aThrough = 0) noexcept;
} // namespace GameRaycast
