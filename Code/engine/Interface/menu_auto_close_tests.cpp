// A live menu closes when the world changes its target (MenuAutoClose; Skyrim Souls RE's auto-close).
#include <catch2/catch.hpp>

#include <Interface/MenuAutoClose.h>

using MenuAutoClose::Target;

TEST_CASE("A live menu stays open while its target stays", "[menus]")
{
    auto watch = MenuAutoClose::Open({.Distance = 120.f});
    CHECK(MenuAutoClose::Check(watch, {.Distance = 150.f}).empty());
    CHECK(MenuAutoClose::Check(watch, {.Distance = MenuAutoClose::kDistance}).empty());
}

TEST_CASE("A live menu closes when its target goes, dies or leaves", "[menus]")
{
    auto watch = MenuAutoClose::Open({.Actor = true, .Distance = 120.f});
    SECTION("disabled or deleted") { CHECK(!MenuAutoClose::Check(watch, {.Gone = true}).empty()); }
    SECTION("killed") { CHECK(!MenuAutoClose::Check(watch, {.Actor = true, .Dead = true, .Distance = 120.f}).empty()); }
    SECTION("out of range") { CHECK(!MenuAutoClose::Check(watch, {.Actor = true, .Distance = 3000.f}).empty()); }
}

TEST_CASE("A menu opened on what already was so keeps it", "[menus]")
{
    SECTION("a corpse's loot stays open on the dead")
    {
        auto watch = MenuAutoClose::Open({.Actor = true, .Dead = true, .Distance = 100.f});
        CHECK(MenuAutoClose::Check(watch, {.Actor = true, .Dead = true, .Distance = 100.f}).empty());
    }
    SECTION("opened far (a script's ShowBarterMenu): closes only once it gets further still")
    {
        auto watch = MenuAutoClose::Open({.Distance = 900.f});
        CHECK(MenuAutoClose::Check(watch, {.Distance = 700.f}).empty());
        CHECK(MenuAutoClose::Check(watch, {.Distance = 790.f}).empty());
        CHECK(!MenuAutoClose::Check(watch, {.Distance = 810.f}).empty());
    }
}
