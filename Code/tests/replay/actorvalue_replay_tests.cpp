#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include "Services/ActorValueService.h"
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Regen attr events update cached actor values", "[Replay][ActorValueService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("actorvalue_regen.ndjson"));

    REQUIRE(events == 3);

    auto& cached = harness.GetActorValueService().GetCachedValues();

    // formId 20 = 0x14 should have an entry created by regen_attr events
    REQUIRE(cached.count(0x14) == 1);

    // regen_attr with type=24 (health) added 5.0
    CHECK(cached.at(0x14).health > 0.0f);

    // regen_attr with type=25 (magicka) added 3.0
    CHECK(cached.at(0x14).magicka > 0.0f);
}
