#pragma once

#include <array>
#include <cstdint>

namespace BodiesLayer
{
// The engine calls that put a copy's own bodies on its owner's layer (Actor::SetBodiesLayer, TaskQueueInterface 36922
// to 36929), in order. Every move keyframes them: a dynamic body poses its bone, in the world or not (78910).
enum class Op : uint8_t
{
    kNone,
    kRemove,     // 36924, out of the world
    kKeyframe,   // 36925, motion type 4
    kSetLayer,   // 36929, the layer and its capsule's group
    kAddToWorld, // 36922
};
using Steps = std::array<Op, 3>;

// Out (layer 0): removed then keyframed, as EndKnockState 39897 ends a living ragdoll; out already but a body still
// dynamic is not done. A layer: keyframed, then set, then added if out, as EnterFurnitureCollision 40063.
[[nodiscard]] constexpr Steps For(uint8_t aLayerNow, uint8_t aLayer, bool aGroupMatches, bool aAnyDynamic, bool aInWorld) noexcept
{
    if (aLayerNow == aLayer && (aLayer ? aGroupMatches : !aAnyDynamic))
        return {};
    if (!aLayer)
        return {Op::kRemove, Op::kKeyframe, Op::kNone};
    return {Op::kKeyframe, Op::kSetLayer, aInWorld ? Op::kNone : Op::kAddToWorld};
}
} // namespace BodiesLayer
