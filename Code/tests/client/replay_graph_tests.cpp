#include <catch2/catch.hpp>
#include <Games/Animation/ReplayGraph.h>

TEST_CASE("A refused action can take its owner's resulting graph", "[animation][replay-graph]")
{
    const bool cLoaded = GENERATE(false, true);
    uint32_t calls = 0;
    const bool cTaken = ReplayGraph::Recover(false, "owner's graph", [&](std::string_view acState) {
        CHECK(acState == "owner's graph");
        ++calls;
        return cLoaded;
    });
    CHECK(calls == 1);
    CHECK(cTaken == cLoaded);
}

TEST_CASE("Accepted actions and missing graph snapshots need no recovery", "[animation][replay-graph]")
{
    const bool cTaken = GENERATE(false, true);
    const auto cState = GENERATE(std::string_view{}, std::string_view{"graph"});
    if (!cTaken && !cState.empty()) return;
    CHECK(ReplayGraph::Recover(cTaken, cState, [](std::string_view) {
        FAIL("No native graph load is needed");
        return false;
    }) == cTaken);
}
