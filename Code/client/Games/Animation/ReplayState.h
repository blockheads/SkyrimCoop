#pragma once

#include <cstdint>

namespace ReplayState
{
constexpr uint32_t kWeaponMask = 7u << 5;

constexpr uint32_t Flags2(uint32_t aOwner, uint32_t aCopy) noexcept
{
    return (aOwner & ~kWeaponMask) | (aCopy & kWeaponMask);
}
} // namespace ReplayState
