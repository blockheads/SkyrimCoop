#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Calendar simulate_time stores calendar pointer", "[Replay][CalendarService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("calendar_simulate.ndjson"));

    REQUIRE(events == 2);

    // calendar this_ptr sentinel stored by simulate_time op
    REQUIRE(harness.GetPointerTable().Contains(0xFFFF0002));

    // exact this_ptr value from fixture
    CHECK(harness.GetPointerTable().Lookup(0xFFFF0002) == 140737488410000ull);

    // Note: Do NOT assert on GetGameHour/GetGameDay -- those are populated by
    // memory reads (always 0 in replay per Pitfall 1).
}
