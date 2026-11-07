#pragma once

// The copy contract for combat, without the engine (docs/re/systems/combat-targets.md).
namespace CopyCombat
{
// Actor::StartCombat (38561): a copy never takes a controller or group, CombatUpdateJob (46902) would run its combat AI.
[[nodiscard]] constexpr bool MayStart(bool aCopy) noexcept
{
    return !aCopy;
}
} // namespace CopyCombat
