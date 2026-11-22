#pragma once

#include <cstdint>

// Our collision groups on loose objects' bodies (docs/re/systems/physics.md, "The collision filter"): which game
// simulates a body decides what it meets here. The engine hands out groups counting up from 1; these are far above.
namespace BodyGroups
{
constexpr uint16_t kPlayed = 0xFFEC; // another game moves it: steered here
constexpr uint16_t kMoving = 0xFFEB; // this game moves it

enum class Meet
{
    kEngine,  // as the engine's filter says
    kCollide,
    kPass,
};

// Loose objects' layers (CommonLibSSE-NG COL_LAYER): clutter, weapon, props, small and large debris, large clutter.
constexpr bool IsLoose(uint32_t aLayer) noexcept
{
    return aLayer == 4 || aLayer == 5 || aLayer == 10 || aLayer == 19 || aLayer == 20 || aLayer == 29;
}

// Whether a body this game starts to move gets the moving group now: it changes what the body meets only against a
// played one (Decide), and writing it makes Havok redo the body's agents (77794), dropping its contacts for a step.
constexpr bool WritesMoving(bool aAnyPlayed) noexcept
{
    return aAnyPlayed;
}

// Two filter infos (layer in the low 7 bits, group in the high 16). A played body meets the world, characters and
// what this game moves; not other played bodies (their owner's game settled those), and not a loose object nobody
// moves: its owner's game never hit that one, or it would be moving too.
constexpr Meet Decide(uint32_t aFilterA, uint32_t aFilterB) noexcept
{
    const uint32_t cGroupA = aFilterA >> 16, cGroupB = aFilterB >> 16;
    const bool cPlayedA = cGroupA == kPlayed, cPlayedB = cGroupB == kPlayed;
    const bool cMovingA = cGroupA == kMoving, cMovingB = cGroupB == kMoving;
    if (cMovingA && cMovingB)
        return Meet::kCollide; // one group, which the engine would keep apart
    if (!cPlayedA && !cPlayedB)
        return Meet::kEngine;
    if (cPlayedA && cPlayedB)
        return Meet::kPass;
    if (cMovingA || cMovingB)
        return Meet::kCollide;
    return IsLoose((cPlayedA ? aFilterB : aFilterA) & 0x7F) ? Meet::kPass : Meet::kEngine;
}
} // namespace BodyGroups
