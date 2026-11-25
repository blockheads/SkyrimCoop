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

TEST_CASE("Only the down camera's own values, with it current, are its", "[clock-wishes]")
{
    CHECK(ClockWishes::FromDownCamera(0.8f, true));
    CHECK(ClockWishes::FromDownCamera(1.f, true));
    CHECK_FALSE(ClockWishes::FromDownCamera(0.25f, true));
    CHECK_FALSE(ClockWishes::FromDownCamera(0.8f, false));
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

TEST_CASE("Another request while down goes to the party and replaces the down camera's", "[clock-wishes]")
{
    ClockWishes wishes;
    CHECK_FALSE(wishes.Ask(0.8f, true));
    CHECK(wishes.Ask(0.25f, false));
    CHECK(wishes.Wanted == 0.25f);
    CHECK(wishes.Clock() == 0.25f);
    CHECK(wishes.Ask(1.f, false));
    CHECK(wishes.Clock() == 1.f);
}

TEST_CASE("The down camera replaces a Slow Time, for the party too", "[clock-wishes]")
{
    ClockWishes wishes;
    CHECK(wishes.Ask(0.3f, false));
    INFO("entering: this game at 0.8 as vanilla's, the party back to 1");
    CHECK(wishes.Ask(0.8f, true));
    CHECK(wishes.Wanted == 1.f);
    CHECK(wishes.Clock() == 0.8f);
    INFO("leaving");
    CHECK_FALSE(wishes.Ask(1.f, true));
    CHECK(wishes.Clock() == 1.f);
}
