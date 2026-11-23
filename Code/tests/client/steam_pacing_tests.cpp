// SteamPacing: Steam paces a P2P connection at a fixed rate, below which ENet resends what is only queued in Steam
// and cuts its throttle (docs/design/network-performance.md, Steam's send rate).
#include <catch2/catch.hpp>

#include <Bridges/SteamPacing.h>

using namespace SteamPacing;

TEST_CASE("Steam puts out ENet's whole window within one frame", "[steam-pacing]")
{
    constexpr double cFrameMs = 1000.0 / 60.0;
    CHECK(WindowDrainMs(kSendRate) < cFrameMs);
    // Steam's default (256 KB/s) and the first fix (1 MB/s): 250 and 62.5 ms, the tail past ENet's ~100 ms resend
    // timer on the duo; transfers stalled at both.
    CHECK_FALSE(WindowDrainMs(256 * 1024) < cFrameMs);
    CHECK_FALSE(WindowDrainMs(1024 * 1024) < cFrameMs);
}

TEST_CASE("Steam's send buffer holds ENet's whole window and more", "[steam-pacing]")
{
    // SendMessage refuses (k_EResultLimitExceeded) past the buffer: a refused datagram is one ENet resends.
    CHECK(kSendBuffer >= 8 * kEnetWindow);
}
