#include <catch2/catch.hpp>
#include <Games/Animation/MachineStates.h>

using namespace MachineStates;

namespace
{
const uint32_t kStanding = Key("QuadrupedBehavior.hkb", "StandingBehavior");
const uint32_t kDefault = Key("QuadrupedBehavior.hkb", "DefaultBehavior");
} // namespace

TEST_CASE("A machine's key is its behaviour and name, the same on every game", "[animation][machine-states]")
{
    CHECK(kStanding == Key("QuadrupedBehavior.hkb", "StandingBehavior"));
    CHECK(kStanding != kDefault);
    CHECK(Key("A.hkb", "StandingBehavior") != Key("B.hkb", "StandingBehavior")); // the same name in two behaviours
    CHECK(Key("ab", "c") != Key("a", "bc"));
}

TEST_CASE("Only a machine each game could set apart is tracked", "[animation][machine-states]")
{
    CHECK(Tracked(0, true));   // a variable-bound start (StandingBehavior <- iSyncTurnState)
    CHECK(Tracked(2, false));  // a random start
    CHECK_FALSE(Tracked(0, false)); // the behaviour file's fixed start: the same everywhere
}

TEST_CASE("Machine states pack and unpack with their own times", "[animation][machine-states]")
{
    const auto cPacked = Pack(7, {{kStanding, 1, 950}, {kDefault, cInactive, 1000}}, 1000);
    uint16_t generation = 0;
    std::vector<Entry> entries;
    REQUIRE(Unpack(cPacked, 1000, generation, entries));
    CHECK(generation == 7);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].Key == kStanding);
    CHECK(entries[0].State == 1);
    CHECK(entries[0].At == 950);
    CHECK(entries[1].State == cInactive);
    CHECK(entries[1].At == 1000);
    CHECK_FALSE(Unpack("bad", 0, generation, entries));
}

TEST_CASE("A copy's graph time trails its playback by its last action's lateness", "[animation][machine-states]")
{
    // A lost packet resent: the turn at owner tick 1000 taken at playback 2600, so everything after it is 1600 late.
    const uint64_t cLag = ReplayLag(2600, 1000);
    CHECK(cLag == 1600);
    CHECK(GraphTime(3100, cLag) == 1500);
    CHECK(ReplayLag(990, 1000) == 0);
    CHECK(GraphTime(2500, 0, 1000) == 1000); // an action due but not taken: the graph is not past it
    CHECK(GraphTime(900, 0, 1000) == 900);
}

TEST_CASE("A copy's machine apart from its owner's is moved after the grace", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add(0, {{kStanding, 1, 1000}});
    timelines.SetGraphTime(1010);
    CHECK_FALSE(timelines.Reconcile(kStanding, 0)); // apart from now
    timelines.SetGraphTime(1010 + cGraceMs - 1);
    CHECK_FALSE(timelines.Reconcile(kStanding, 0)); // its own replay may still get there
    timelines.SetGraphTime(1010 + cGraceMs);
    CHECK(timelines.Reconcile(kStanding, 0) == 1);
    CHECK_FALSE(timelines.Reconcile(kStanding, 0)); // tried: another try only after another grace
    CHECK_FALSE(timelines.Reconcile(kStanding, 1)); // there
}

TEST_CASE("A copy's machine that gets there on its own is left alone", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add(0, {{kStanding, 1, 1000}});
    timelines.SetGraphTime(1010);
    CHECK_FALSE(timelines.Reconcile(kStanding, 0));
    timelines.SetGraphTime(1100);
    CHECK_FALSE(timelines.Reconcile(kStanding, 1)); // its replayed transition came
    timelines.SetGraphTime(1100 + cGraceMs);
    CHECK_FALSE(timelines.Reconcile(kStanding, 1));
}

TEST_CASE("The owner's state is the one at the copy's graph time, whatever arrived first", "[animation][machine-states]")
{
    Timelines timelines;
    // Two changes 100 ms apart, the later one received first: order of arrival does not matter, owner time does.
    timelines.Add(0, {{kStanding, 0, 1100}});
    timelines.Add(0, {{kStanding, 1, 1000}});
    timelines.SetGraphTime(1050);
    CHECK(timelines.Wanted(kStanding)->State == 1);
    timelines.SetGraphTime(1150);
    CHECK(timelines.Wanted(kStanding)->State == 0);
}

TEST_CASE("No state is wanted while the owner's machine is inactive, or before it has one", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add(0, {{kStanding, 1, 1000}, {kStanding, cInactive, 1200}});
    timelines.SetGraphTime(900);
    CHECK_FALSE(timelines.Wanted(kStanding)); // the copy started early: its owner's has not activated yet
    timelines.SetGraphTime(1100);
    CHECK(timelines.Wanted(kStanding)->State == 1);
    timelines.SetGraphTime(1300);
    CHECK_FALSE(timelines.Wanted(kStanding)); // a copy-only activation: left to its parent machine
    CHECK_FALSE(timelines.Reconcile(kStanding, 0));
}

TEST_CASE("A newer owner state restarts the grace", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add(0, {{kStanding, 1, 1000}});
    timelines.SetGraphTime(1000);
    CHECK_FALSE(timelines.Reconcile(kStanding, 0));
    timelines.Add(0, {{kStanding, 2, 1200}});
    timelines.SetGraphTime(1200 + cGraceMs - 1);
    CHECK_FALSE(timelines.Reconcile(kStanding, 0)); // apart since 1000, but the owner entered 2 only at 1200
    timelines.SetGraphTime(1200 + cGraceMs);
    CHECK(timelines.Reconcile(kStanding, 0) == 2);
}

TEST_CASE("Another owner graph generation replaces what the copy knew", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add(0, {{kStanding, 1, 1000}});
    timelines.Add(1, {{kDefault, 0, 1100}});
    timelines.SetGraphTime(1200);
    CHECK_FALSE(timelines.Wanted(kStanding));
    CHECK(timelines.Wanted(kDefault)->State == 0);
    timelines.Clear();
    CHECK_FALSE(timelines.Any());
}

TEST_CASE("Passed owner states are history", "[animation][machine-states]")
{
    Timelines timelines;
    for (uint64_t at = 0; at < 1000; at += 10)
        timelines.Add(0, {{kStanding, int32_t(at / 10 % 3), at}});
    timelines.SetGraphTime(2000);
    CHECK(timelines.Wanted(kStanding)->At == 990);
}

TEST_CASE("An owner's unsent states are bounded", "[animation][machine-states]")
{
    Outbox outbox;
    for (int32_t i = 0; i < int32_t(cMost) + 5; ++i)
        outbox.Push({kStanding, i, uint64_t(i)});
    const auto cDrained = outbox.Drain();
    REQUIRE(cDrained.size() == cMost);
    CHECK(cDrained.front().State == 5);
    CHECK(outbox.Drain().empty());
}
