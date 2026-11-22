#pragma once

#include <cstdint>

// Actor critical stages (Actor+0xF0; CommonLibSSE-NG ACTOR_CRITICAL_STAGE), set by Actor::SetCriticalStage (37615)
// and applied by 37616 (docs/re/systems/death-disintegrate.md).
namespace CriticalStage
{
constexpr uint32_t kNone = 0;
constexpr uint32_t kGooStart = 1;
constexpr uint32_t kGooEnd = 2;
constexpr uint32_t kDisintegrateStart = 3;
constexpr uint32_t kDisintegrateEnd = 4;

// 37616 sets the actor's 3D to none: an ash pile or goo takes its place.
[[nodiscard]] constexpr bool RemovesBody(uint32_t aStage) noexcept
{
    return aStage == kGooEnd || aStage == kDisintegrateEnd;
}

// This game's player never turns to ash or goo: without its 3D the frame waits on its update for good.
[[nodiscard]] constexpr bool Takes(bool aThisGamesPlayer, uint32_t aStage) noexcept
{
    return !aThisGamesPlayer || aStage == kNone;
}

// Actor.AttachAshPile (54655): a player that keeps its body leaves no ash pile.
[[nodiscard]] constexpr bool TakesAshPile(bool aThisGamesPlayer) noexcept
{
    return !aThisGamesPlayer;
}
} // namespace CriticalStage
