#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Combat events increment damage and projectile counters", "[Replay][CombatService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("combat_damage.ndjson"));

    REQUIRE(events == 5);

    REQUIRE(harness.GetCombatService().GetDamageEventCount() >= 1);
    REQUIRE(harness.GetCombatService().GetProjectileEventCount() >= 1);

    // target actor 256 = 0x100 created by character_ctor
    CHECK(harness.GetPointerTable().Contains(0x100));
}
