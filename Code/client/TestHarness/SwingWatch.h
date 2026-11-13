#pragma once

#include <Havok/SwingCast.h>

#include <cstdint>

// The player's last melee swing as the engine cast it (Actor::CalculateCurrentHitTargetForWeaponSwing 38628's
// LinearCastFrom 25931): where it started, where it looked, and what it met, nearest first.
namespace TestHarness::SwingWatch
{
struct Swing
{
    uint64_t Count{}; // the player's swing casts so far
    GameSwingCast::Cast Cast;
};
[[nodiscard]] Swing Last() noexcept;
} // namespace TestHarness::SwingWatch
