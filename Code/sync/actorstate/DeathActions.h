#pragma once

#include <cstdint>
#include <utility>

namespace DeathActions
{
// ActorState1::lifeState, also carried in ActionEvent::State1 (CommonLibSSE-NG ACTOR_LIFE_STATE).
constexpr uint32_t kDying = 1u << 21;
constexpr bool IsDying(uint32_t aState) noexcept
{
    return (aState >> 21 & 0xF) == 1;
}
// Resurrect runs a native reset action before changing life state; it must stay local even on an owner.
template <class Resurrect> void WithLocalReset(bool& aForcedAction, Resurrect&& aResurrect)
{
    const bool cPrevious = std::exchange(aForcedAction, true);
    aResurrect();
    aForcedAction = cPrevious;
}
} // namespace DeathActions
