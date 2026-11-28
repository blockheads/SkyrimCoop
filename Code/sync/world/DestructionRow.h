#pragma once

#include <bit>
#include <cstdint>

// The kDestruction row's value (ReferenceStates): a destructible's health (ExtraObjectHealth 0x55, its base's health
// when it has none), as float bits. docs/re/systems/destruction.md.
namespace DestructionRow
{
[[nodiscard]] constexpr uint32_t Pack(float aHealth) noexcept
{
    return std::bit_cast<uint32_t>(aHealth);
}

[[nodiscard]] constexpr float Unpack(uint32_t aValue) noexcept
{
    return std::bit_cast<float>(aValue);
}

// The host takes a guest's proposal only as damage: a hit only lowers health, and the host's own hits may be ahead.
[[nodiscard]] constexpr bool GuestDamages(uint32_t aHere, uint32_t aProposed) noexcept
{
    return Unpack(aProposed) < Unpack(aHere);
}
} // namespace DestructionRow
