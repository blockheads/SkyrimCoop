#include <catch2/catch.hpp>
#include "../FakeNativeVm.h"

TEST_CASE("Registered launch natives belong to their own VM's game heap", "[papyrus][native-lifetime]")
{
    FakeNativeVm first;
    FakeNativeVm second;
    REQUIRE(first.LaunchCheck());
    REQUIRE(second.LaunchCheck());
    REQUIRE(first.Allocations == 1);
    REQUIRE(second.Allocations == 1);

    first.ReleaseTypes();
    CHECK(first.Destructions == 1);
    CHECK(first.GameHeapFrees == 1);
    CHECK(second.Destructions == 0);
    CHECK(second.GameHeapFrees == 0);
    REQUIRE(second.LaunchCheck());

    second.ReleaseTypes();
    CHECK(second.Destructions == 1);
    CHECK(second.GameHeapFrees == 1);
}
