#pragma once

#include <cstdint>

// An actor's weapon/magic readiness, as the engine tracks it (ActorState flags2 bits 5-7).
enum class WeaponState : uint8_t
{
    Sheathed = 0,
    WantToDraw = 1,
    Drawing = 2,
    Drawn = 3,
    WantToSheathe = 4,
    Sheathing = 5,
};

// The hands' direction as the actor chose it: up from the moment a draw starts until a sheathe
// starts (the choice is final when it is made, the animation follows).
constexpr bool HandsUp(WeaponState aState) noexcept
{
    return aState == WeaponState::WantToDraw || aState == WeaponState::Drawing || aState == WeaponState::Drawn;
}

// What the actor shows: the hands stay up until the sheathe animation starts.
constexpr bool ShowsHandsUp(WeaponState aState) noexcept
{
    return aState == WeaponState::Drawn || aState == WeaponState::WantToSheathe;
}

// A draw or sheathe animation is playing. (WantToDraw/WantToSheathe are requests: on a copy, whose
// own draw/sheathe actions are refused, they can stay pending for good.)
constexpr bool IsAnimating(WeaponState aState) noexcept
{
    return aState == WeaponState::Drawing || aState == WeaponState::Sheathing;
}
