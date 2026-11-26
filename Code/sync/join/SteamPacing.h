#pragma once

#include <cstdint>

// The send rate Steam paces each P2P connection at, fixed (no bandwidth estimation): it must put out ENet's whole
// reliable window well inside ENet's resend timer (docs/design/network-performance.md, Steam's send rate).
namespace SteamPacing
{
inline constexpr int32_t kSendRate = 4 * 1024 * 1024; // SendRateMin = SendRateMax, bytes/s
inline constexpr int32_t kSendBuffer = 2 * 1024 * 1024;
inline constexpr int32_t kEnetWindow = 64 * 1024; // ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE

// How long Steam takes to put out ENet's full window at aRate.
[[nodiscard]] constexpr double WindowDrainMs(int32_t aRate) noexcept
{
    return 1000.0 * kEnetWindow / aRate;
}
} // namespace SteamPacing
