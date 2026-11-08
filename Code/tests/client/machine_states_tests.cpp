#include <catch2/catch.hpp>
#include <Games/Animation/MachineStates.h>

using namespace MachineStates;

namespace
{
const uint32_t kStanding = Key("StandingBehavior", "LoopingTurnRight,StandingIdleState");
const uint32_t kDefault = Key("DefaultBehavior", "CannedTurnState,StandingState");

Machine At(int32_t aState, std::optional<int32_t> aHeading = std::nullopt)
{
    return {aState, aHeading};
}
} // namespace

TEST_CASE("A machine's key is its name and its states' names", "[animation][machine-states]")
{
    CHECK(kStanding == Key("StandingBehavior", "LoopingTurnRight,StandingIdleState"));
    CHECK(kStanding != kDefault);
    CHECK(Key("StandingBehavior", "A,B") != Key("StandingBehavior", "A,C")); // the same name, other states
    CHECK(Key("ab", "c") != Key("a", "bc"));
    KeyHash hash; // as MachineKeyOf reads a clone
    hash.Add("StandingBehavior");
    hash.Add("|");
    hash.Add("LoopingTurnRight");
    hash.Add(",");
    hash.Add("StandingIdleState");
    CHECK(hash.Value == kStanding);
}

TEST_CASE("Machine states pack whole state ids, generations and times", "[animation][machine-states]")
{
    const auto cPacked = Pack({{kStanding, 65536, 950, 3}, {kDefault, cInactive, 1000, 3}, {kDefault, 65535, 1000, 4}}, 1000);
    std::vector<Entry> entries;
    REQUIRE(Unpack(cPacked, 1000, entries));
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].Key == kStanding);
    CHECK(entries[0].State == 65536); // a mod's state id, not 0
    CHECK(entries[0].At == 950);
    CHECK(entries[0].Generation == 3);
    CHECK(entries[1].State == cInactive);
    CHECK(entries[2].State == 65535); // not taken for inactive
    CHECK(entries[2].Generation == 4);
    CHECK_FALSE(Unpack("bad", 0, entries));
    CHECK_FALSE(Unpack(std::string((cMostBatchEntries + 1) * cEntryBytes, '\0'), 0, entries)); // more than an owner sends
}

TEST_CASE("A copy's graph time trails its playback by a late action's lateness, spent as it plays on", "[animation][machine-states]")
{
    // A lost packet resent: the turn at owner tick 1000 taken at playback 2600.
    const uint64_t cLag = ReplayLag(2600, 1000);
    CHECK(cLag == 1600);
    CHECK(GraphTime(2600, cLag, 2600) == 1000);
    CHECK(GraphTime(3000, cLag, 2600) == 1800); // 400 of it spent: the graph catches up twice as fast
    CHECK(GraphTime(5000, cLag, 2600) == 5000); // caught up: no later action needed
    CHECK(ReplayLag(990, 1000) == 0);
    CHECK(GraphTime(2500, 0, 0, 1000) == 1000); // an action due but not taken: the graph is not past it
}

TEST_CASE("A copy's machine apart from its owner's is moved after the grace", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add({{kStanding, 1, 1000}});
    timelines.SetGraphTime(1010);
    CHECK_FALSE(timelines.Reconcile(kStanding, At(0))); // apart from now
    timelines.SetGraphTime(1010 + cGraceMs - 1);
    CHECK_FALSE(timelines.Reconcile(kStanding, At(0))); // its own replay may still get there
    timelines.SetGraphTime(1010 + cGraceMs);
    CHECK(timelines.Reconcile(kStanding, At(0)) == 1);
    CHECK_FALSE(timelines.Reconcile(kStanding, At(0))); // tried: another try only after another grace
    CHECK_FALSE(timelines.Reconcile(kStanding, At(1))); // there
}

TEST_CASE("A machine is where its running transition heads", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add({{kStanding, 1, 1000}});
    timelines.SetGraphTime(1000);
    // Heading to the owner's state, though its current state is still the source (a delayed state change): not apart.
    CHECK_FALSE(timelines.Reconcile(kStanding, At(0, 1)));
    timelines.SetGraphTime(1000 + cGraceMs);
    CHECK_FALSE(timelines.Reconcile(kStanding, At(0, 1)));
    // Reading the owner's state but heading elsewhere: apart, so the wrong blend is cut after the grace.
    CHECK_FALSE(timelines.Reconcile(kStanding, At(1, 2)));
    timelines.SetGraphTime(1000 + 2 * cGraceMs);
    CHECK(timelines.Reconcile(kStanding, At(1, 2)) == 1);
}

TEST_CASE("The owner's state is the one at the copy's graph time, whatever arrived first", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add({{kStanding, 0, 1100}});
    timelines.Add({{kStanding, 1, 1000}});
    timelines.SetGraphTime(1050);
    CHECK(timelines.Wanted(kStanding)->State == 1);
    timelines.SetGraphTime(1150);
    CHECK(timelines.Wanted(kStanding)->State == 0);
}

TEST_CASE("No state is wanted while the owner's machine is inactive, or before it has one", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add({{kStanding, 1, 1000}, {kStanding, cInactive, 1200}});
    timelines.SetGraphTime(900);
    CHECK_FALSE(timelines.Wanted(kStanding)); // the copy started early: its owner's has not activated yet
    timelines.SetGraphTime(1100);
    CHECK(timelines.Wanted(kStanding)->State == 1);
    timelines.SetGraphTime(1300);
    CHECK_FALSE(timelines.Wanted(kStanding)); // its parent, tracked too, is moved instead
}

TEST_CASE("A newer owner state restarts the grace", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add({{kStanding, 1, 1000}});
    timelines.SetGraphTime(1000);
    CHECK_FALSE(timelines.Reconcile(kStanding, At(0)));
    timelines.Add({{kStanding, 2, 1200}});
    timelines.SetGraphTime(1200 + cGraceMs - 1);
    CHECK_FALSE(timelines.Reconcile(kStanding, At(0)));
    timelines.SetGraphTime(1200 + cGraceMs);
    CHECK(timelines.Reconcile(kStanding, At(0)) == 2);
}

TEST_CASE("History survives the graph time stepping back", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add({{kStanding, 0, 1000}, {kStanding, 1, 1800}});
    timelines.SetGraphTime(2000);
    timelines.SetGraphTime(1500); // a late action came: the graph is back at its tick
    CHECK(timelines.Wanted(kStanding)->State == 0);
    timelines.SetGraphTime(1800 + cHistoryMs + 1);
    timelines.SetGraphTime(1500);
    CHECK_FALSE(timelines.Wanted(kStanding)); // far enough back, history is gone
}

TEST_CASE("One machine's changes do not push out another's state", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add({{kDefault, 1, 100}});
    for (uint64_t at = 1000; at < 1000 + 10 * cMostPerMachine * 10; at += 10)
        timelines.Add({{kStanding, int32_t(at / 10 % 2), at}});
    timelines.SetGraphTime(5000);
    CHECK(timelines.Wanted(kDefault)->State == 1);
}

TEST_CASE("A new owner graph generation takes over at its own time", "[animation][machine-states]")
{
    Timelines timelines;
    timelines.Add({{kStanding, 1, 1000, 0}, {kDefault, 1, 1000, 0}});
    timelines.Add({{kDefault, 0, 2000, 1}}); // the owner's graph rebuilt at 2000: StandingBehavior not in it yet
    timelines.SetGraphTime(1500);
    CHECK(timelines.Wanted(kStanding)->State == 1); // still the old graph at this graph time
    timelines.SetGraphTime(2100);
    CHECK_FALSE(timelines.Wanted(kStanding)); // the old graph's machine
    CHECK(timelines.Wanted(kDefault)->State == 0);
    timelines.Clear();
    CHECK_FALSE(timelines.Any());
}

TEST_CASE("An owner sends its changes, or all its states when asked", "[animation][machine-states]")
{
    Outbox outbox;
    outbox.Push({kStanding, 0, 10});
    outbox.Push({kDefault, 1, 20});
    outbox.Push({kStanding, 1, 30});
    CHECK(outbox.Drain().size() == 3);
    CHECK(outbox.Drain().empty()); // nothing changed
    outbox.RequestBaseline();
    const auto cBaseline = outbox.Drain();
    REQUIRE(cBaseline.size() == 2); // each machine's current state, with the time it entered it
    CHECK(std::ranges::any_of(cBaseline, [](const Entry& acEntry) { return acEntry.Key == kStanding && acEntry.State == 1 && acEntry.At == 30; }));
}

TEST_CASE("Unsent changes that overflow become a baseline", "[animation][machine-states]")
{
    Outbox outbox;
    outbox.Push({kDefault, 1, 1});
    for (int32_t i = 0; i < int32_t(cMostPending) + 1; ++i)
        outbox.Push({kStanding, i, uint64_t(10 + i)});
    const auto cDrained = outbox.Drain();
    REQUIRE(cDrained.size() == 2); // the quiet machine's state is not lost
    CHECK(std::ranges::any_of(cDrained, [](const Entry& acEntry) { return acEntry.Key == kDefault; }));
}

TEST_CASE("A rebuilt owner graph sends the new graph's states alone", "[animation][machine-states]")
{
    Outbox outbox;
    outbox.Push({kDefault, 1, 1, 0});
    outbox.Rebuilt();
    outbox.Push({kStanding, 1, 5, 1});
    const auto cDrained = outbox.Drain();
    REQUIRE(cDrained.size() == 1);
    CHECK(cDrained[0].Key == kStanding);
    CHECK(cDrained[0].Generation == 1);
}
