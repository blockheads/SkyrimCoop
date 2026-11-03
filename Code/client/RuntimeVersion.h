#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace SkyrimRuntime
{
#if COOP_SKYRIM_1_6_1170
inline constexpr std::array<int, 4> Version{1, 6, 1170, 0};
inline constexpr std::string_view Name = "1.6.1170.0";
inline constexpr size_t PlayerShift = 0;
inline constexpr size_t VmShift = 0;
#else
inline constexpr std::array<int, 4> Version{1, 7, 104, 0};
inline constexpr std::string_view Name = "1.7.104.0";
inline constexpr size_t PlayerShift = 0x8;
inline constexpr size_t VmShift = 0x10;
#endif
inline constexpr uint32_t PackedVersion = (Version[0] << 24) | (Version[1] << 16) | (Version[2] << 4) | Version[3];
}
