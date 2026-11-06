#pragma once

namespace DeathDrops
{
// Actor::DropHeldItemsOnDeath (37320) skipped for a player's character in a session: what it held stays in its hands,
// out of reach, and its respawn equips it again (docs/re/systems/death-drops.md).
[[nodiscard]] constexpr bool Kept(bool aPlayer, bool aOnline) noexcept
{
    return aPlayer && aOnline;
}
} // namespace DeathDrops
