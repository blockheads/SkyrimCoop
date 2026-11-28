// The kDestruction row's value and the host's rule for a guest's (sync/world/DestructionRow.h).
#include <catch2/catch.hpp>

#include <world/DestructionRow.h>

using namespace DestructionRow;

TEST_CASE("The destruction row keeps a destructible's health", "[world_state]")
{
    CHECK(Unpack(Pack(10.f)) == 10.f);
    CHECK(Unpack(Pack(0.f)) == 0.f);
    CHECK(Unpack(Pack(2.5f)) == 2.5f);
}

TEST_CASE("The host takes a guest's destruction only as damage", "[world_state]")
{
    CHECK(GuestDamages(Pack(10.f), Pack(4.f)));
    CHECK(GuestDamages(Pack(4.f), Pack(0.f)));
    CHECK_FALSE(GuestDamages(Pack(4.f), Pack(4.f)));  // the host's own hit already there
    CHECK_FALSE(GuestDamages(Pack(0.f), Pack(10.f))); // a guest's whole one never mends the host's
}
