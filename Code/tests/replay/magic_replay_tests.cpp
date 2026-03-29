#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Spell cast and interrupt cycle tracks active casts", "[Replay][MagicService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("magic_spellcast.ndjson"));

    REQUIRE(events == 5);

    // Sequence: spell_cast (+1), interrupt_cast (-1), spell_cast (+1), apply_effect (no change)
    // One active cast remains: the second spell_cast was not interrupted
    REQUIRE(harness.GetMagicService().GetActiveCastCount() == 1);
}
