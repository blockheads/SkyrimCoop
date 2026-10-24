#pragma once

#include <cstdint>

namespace DeathActions
{
// ActorState1::lifeState, also carried in ActionEvent::State1 (CommonLibSSE-NG ACTOR_LIFE_STATE).
constexpr uint32_t kDying = 1u << 21;
constexpr bool IsDying(uint32_t aState) noexcept
{
    return (aState >> 21 & 0xF) == 1;
}
} // namespace DeathActions
