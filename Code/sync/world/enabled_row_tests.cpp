// The kEnabled row's value: enabled, deleted and destroyed (sync/world/EnabledRow.h).
#include <catch2/catch.hpp>

#include <world/EnabledRow.h>

using namespace EnabledRow;

TEST_CASE("The enabled row keeps each flag apart", "[world_state]")
{
    CHECK(Pack({}) == 1);
    CHECK(Pack({.Enabled = false}) == 0);
    CHECK(Pack({.Enabled = false, .Deleted = true}) == 2);
    const State cTripped = Unpack(Pack({.Destroyed = true}));
    CHECK(cTripped.Enabled);
    CHECK(cTripped.Destroyed);
    CHECK_FALSE(cTripped.Deleted);
    CHECK_FALSE(Unpack(Pack({.Enabled = false, .Destroyed = true})).Enabled);
}
