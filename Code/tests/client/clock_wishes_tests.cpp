#include <Games/ClockWishes.h>

#include <catch2/catch.hpp>

TEST_CASE("A kill cam's slowdown goes to the party, once per change", "[clock-wishes]")
{
    ClockWishes wishes;
    CHECK(wishes.Ask(0.25f, false));
    CHECK_FALSE(wishes.Ask(0.25f, false));
    CHECK(wishes.Clock() == 0.25f);
    CHECK(wishes.Ask(1.f, false));
    CHECK(wishes.Clock() == 1.f);
}

TEST_CASE("The down camera's slowdown stays this game's", "[clock-wishes]")
{
    ClockWishes wishes;
    CHECK_FALSE(wishes.Ask(0.8f, true));
    CHECK(wishes.Clock() == 0.8f);
    wishes.Shared = 0.25f;
    CHECK(wishes.Clock() == 0.25f);
    wishes.Shared = 1.f;
    CHECK_FALSE(wishes.Ask(1.f, true));
    CHECK(wishes.Clock() == 1.f);
}

TEST_CASE("Leaving the down camera ends a Slow Time it cut short, for the party too", "[clock-wishes]")
{
    ClockWishes wishes;
    CHECK(wishes.Ask(0.3f, false));
    CHECK_FALSE(wishes.Ask(0.8f, true));
    CHECK(wishes.Ask(1.f, true));
    CHECK(wishes.Clock() == 1.f);
}

TEST_CASE("A request with the camera up ends a down camera left without its exit", "[clock-wishes]")
{
    ClockWishes wishes;
    CHECK_FALSE(wishes.Ask(0.8f, true));
    CHECK_FALSE(wishes.Ask(1.f, false));
    CHECK(wishes.Clock() == 1.f);
}
