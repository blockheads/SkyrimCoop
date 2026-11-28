#pragma once

namespace DeathDrops
{
// Actor::DropHeldItemsOnDeath (37320) skipped for a player's character in a session: what it held stays in its hands,
// out of reach, and its respawn equips it again (docs/re/systems/death-drops.md).
[[nodiscard]] constexpr bool Kept(bool aPlayer, bool aOnline) noexcept
{
    return aPlayer && aOnline;
}

// What a kept body's hands hold does not weigh into a grab of its corpse (NiAVObject::GetTotalMass 26020, by which
// CreateMouseSprings 40555 weakens the spring): it drags as an NPC's corpse, whose weapon fell (physics.md).
[[nodiscard]] constexpr bool HeldWeighs(bool aKept, bool aDead) noexcept
{
    return !(aKept && aDead);
}
} // namespace DeathDrops
