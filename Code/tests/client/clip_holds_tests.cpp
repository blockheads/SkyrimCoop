// The host's script-animated objects out of its sight (docs/design/traps.md): held for a while, each animation again.
#include <catch2/catch.hpp>

#include <Games/Skyrim/ClipHolds.h>

TEST_CASE("An animated object is held for a clip's time, from its last animation", "[ClipHolds]")
{
    ClipHolds<int> holds;
    const ClipHolds<int>::TimePoint cStart{};
    CHECK_FALSE(holds.Held(1, cStart));
    holds.Hold(1, cStart);
    CHECK(holds.Held(1, cStart + std::chrono::seconds(14)));
    CHECK_FALSE(holds.Held(2, cStart));
    CHECK_FALSE(holds.Held(1, cStart + ClipHolds<int>::kHeld));
    holds.Hold(1, cStart + std::chrono::seconds(10));
    CHECK(holds.Held(1, cStart + std::chrono::seconds(20)));
}
