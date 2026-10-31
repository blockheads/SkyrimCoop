#include <BSCore/LockOrderAttribution.h>
#include <catch2/catch.hpp>

#include <array>

TEST_CASE("Pass-through travel frames do not hide a real engine callback", "[locks]")
{
    using namespace LockOrderAttribution;
    constexpr Range module{0x1000, 0x3000};
    constexpr std::array passThrough{Range{0x2000, 0x2358}};
    // Checker -> engine locks -> travel wrapper -> engine main loop.
    constexpr std::array<uintptr_t, 4> native{0x1100, 0x5000, 0x2200, 0x6000};
    CHECK(IntroducedByUs(native, module, {}));
    CHECK_FALSE(IntroducedByUs(native, module, passThrough));
    // A service called by the engine still introduces its own order, inside or below that wrapper.
    constexpr std::array<uintptr_t, 5> callback{0x1100, 0x5000, 0x1800, 0x2200, 0x6000};
    constexpr std::array<uintptr_t, 5> outer{0x1100, 0x5000, 0x2200, 0x1800, 0x6000};
    CHECK(IntroducedByUs(callback, module, passThrough));
    CHECK(IntroducedByUs(outer, module, passThrough));
    constexpr std::array<uintptr_t, 3> boundary{0x1100, 0x5000, 0x2358};
    CHECK(IntroducedByUs(boundary, module, passThrough));
}
