#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include "Services/CharacterService.h"  // LocalComponent
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Full session replay: no crashes, aggregate state check", "[Replay][Integration]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("live_session.ndjson"));

    INFO("Total events replayed: " << events);
    // live_session.ndjson has ~37k non-ctrl events (45k total minus ~8k ctrl_heartbeat/ctrl_ready)
    REQUIRE(events > 30000);

    // PointerTable should have tracked actors
    REQUIRE(harness.GetPointerTable().Size() > 0);

    // Player (formId=0x14) should always be in the table
    REQUIRE(harness.GetPointerTable().Contains(0x14));

    // Registry should have entities with LocalComponent
    auto& reg = harness.GetRegistry();
    auto view = reg.view<LocalComponent>();
    size_t entityCount = 0;
    for ([[maybe_unused]] auto entity : view) entityCount++;

    INFO("Total entities with LocalComponent: " << entityCount);
    REQUIRE(entityCount > 0);

    // CharacterService should have processed events
    // (this is a smoke check that the full pipeline works end-to-end)
    INFO("PointerTable size: " << harness.GetPointerTable().Size());
    CHECK(harness.GetPointerTable().Size() > 5);
}
