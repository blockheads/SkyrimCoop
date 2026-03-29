#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Quest service survives construction and replay without crash", "[Replay][QuestService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("quest_survival.ndjson"));

    REQUIRE(events == 2);

    INFO("QuestService has no routed ops -- this test verifies survival only");

    // player actor created by character_ctor
    CHECK(harness.GetPointerTable().Contains(0x14));
}
