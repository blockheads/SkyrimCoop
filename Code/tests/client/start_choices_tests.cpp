#include <catch2/catch.hpp>
#include <Games/Animation/StartChoices.h>

using namespace StartChoices;

TEST_CASE("Only a start the copy could make otherwise is shared", "[animation][start-choices]")
{
    CHECK(Shared(-1, 0, true));   // a variable-bound start (StandingBehavior <- iSyncTurnState)
    CHECK(Shared(-1, 2, false));  // a random start
    CHECK_FALSE(Shared(-1, 0, false)); // the behaviour file's start: the same on both games
    CHECK_FALSE(Shared(0, 0, true));   // a nested-state transition's: its event reaches the copy too
}

TEST_CASE("Start choices pack and unpack", "[animation][start-choices]")
{
    const auto cChoices = Unpack(Pack({{"StandingBehavior", 1, 0}, {"DefaultBehavior", 2, 0}}), 77);
    REQUIRE(cChoices.size() == 2);
    CHECK(cChoices[0].Machine == "StandingBehavior");
    CHECK(cChoices[0].State == 1);
    CHECK(cChoices[1].Machine == "DefaultBehavior");
    CHECK(cChoices[1].State == 2);
    CHECK(cChoices[1].At == 77);
    CHECK(Unpack("", 0).empty());
}

TEST_CASE("A copy takes its owner's choice nearest its playback time", "[animation][start-choices]")
{
    Queue queue;
    queue.Push({"DefaultBehavior", 0, 1000});
    queue.Push({"StandingBehavior", 1, 1000});
    queue.Push({"StandingBehavior", 0, 1100});
    CHECK(queue.Take("StandingBehavior", 1090) == 0);
    CHECK(queue.Take("StandingBehavior", 1090) == 1);
    CHECK_FALSE(queue.Take("StandingBehavior", 1090));
    CHECK(queue.Take("DefaultBehavior", 1090) == 0);
}

TEST_CASE("A choice that arrived after its activation is not the next one's", "[animation][start-choices]")
{
    // The copy's activation A at playback 1000 found nothing; A's choice came later, then B's for its own activation.
    Queue queue;
    queue.Push({"StandingBehavior", 0, 1000});
    queue.Push({"StandingBehavior", 1, 1600});
    CHECK(queue.Take("StandingBehavior", 1600) == 1);
    CHECK_FALSE(queue.Take("StandingBehavior", 1600)); // A's was passed and dropped
}

TEST_CASE("A choice the copy's playback has not reached waits", "[animation][start-choices]")
{
    Queue queue;
    queue.Push({"StandingBehavior", 2, 2000});
    CHECK_FALSE(queue.Take("StandingBehavior", 2000 - cAheadMs - 1));
    CHECK(queue.Take("StandingBehavior", 2000 - cAheadMs) == 2);
    queue.Push({"StandingBehavior", 1, 3000});
    CHECK_FALSE(queue.Take("StandingBehavior", 3000 + cPastMs + 1));
}

TEST_CASE("An owner's unsent choices are bounded", "[animation][start-choices]")
{
    Queue queue;
    for (int32_t i = 0; i < int32_t(cMost) + 5; ++i)
        queue.Push({"StandingBehavior", i, 0});
    const auto cDrained = queue.Drain();
    REQUIRE(cDrained.size() == cMost);
    CHECK(cDrained.front().State == 5);
    CHECK(queue.Drain().empty());
}

TEST_CASE("An owner records its start, its copy enters it", "[animation][start-choices]")
{
    Queue owner, copy;
    CHECK(Choose(owner, false, "StandingBehavior", 1, 0) == 1);
    for (auto& choice : Unpack(Pack(owner.Drain()), 200)) // the point's owner tick
        copy.Push(std::move(choice));
    CHECK(Choose(copy, true, "StandingBehavior", 0, 180) == 1); // its own variable said 0
    CHECK(Choose(copy, true, "StandingBehavior", 0, 180) == 0); // none left: the engine's
}

TEST_CASE("A choice that comes after its start corrects the copy's machine", "[animation][start-choices]")
{
    Queue queue;
    // The copy's machine started at playback 1000 before its owner's choice came: it entered its own 0.
    CHECK(Choose(queue, true, "StandingBehavior", 0, 1000) == 0);
    CHECK_FALSE(queue.HasCorrections());
    queue.Receive({"StandingBehavior", 1, 1080}, 1150);
    REQUIRE(queue.HasCorrections());
    CHECK_FALSE(queue.Correct("DefaultBehavior", 0, 1160));
    CHECK(queue.Correct("StandingBehavior", 0, 1160) == 1);
    CHECK_FALSE(queue.HasCorrections());
    CHECK_FALSE(queue.Take("StandingBehavior", 1160)); // the choice was its start's, not the next one's
}

TEST_CASE("A late choice the copy already agreed with changes nothing", "[animation][start-choices]")
{
    Queue queue;
    Choose(queue, true, "StandingBehavior", 1, 1000);
    queue.Receive({"StandingBehavior", 1, 1080}, 1150);
    CHECK_FALSE(queue.HasCorrections());
    CHECK_FALSE(queue.Take("StandingBehavior", 1150));
}

TEST_CASE("A machine that left the state it entered is not corrected", "[animation][start-choices]")
{
    Queue queue;
    Choose(queue, true, "StandingBehavior", 0, 1000);
    queue.Receive({"StandingBehavior", 1, 1080}, 1150);
    CHECK_FALSE(queue.Correct("StandingBehavior", 2, 1160)); // a replayed event moved it on meanwhile
    CHECK_FALSE(queue.HasCorrections());
}

TEST_CASE("A late choice matches only a start near its time", "[animation][start-choices]")
{
    Queue queue;
    Choose(queue, true, "StandingBehavior", 0, 1000);
    queue.Receive({"StandingBehavior", 1, 1000 + cAheadMs + 1}, 1400); // too far after: the next start's
    CHECK_FALSE(queue.HasCorrections());
    CHECK(queue.Take("StandingBehavior", 1000 + cAheadMs + 1) == 1);
    Choose(queue, true, "StandingBehavior", 0, 5000);
    queue.Receive({"StandingBehavior", 1, 5050}, 5000 + cLateMs + 1); // came too late to trust
    CHECK_FALSE(queue.HasCorrections());
}

TEST_CASE("A copy's graph time trails its playback by its last action's lateness", "[animation][start-choices]")
{
    // A lost packet resent: the turn at owner tick 1000 taken at playback 2600, so the start after it is 1600 late too.
    const uint64_t cLag = ReplayLag(2600, 1000);
    CHECK(cLag == 1600);
    CHECK(GraphTime(3100, cLag) == 1500);
    CHECK(ReplayLag(990, 1000) == 0);
    // An action due but not yet taken: the graph is not past it, whatever its playback.
    CHECK(GraphTime(2500, 0, 1000) == 1000);
    CHECK(GraphTime(900, 0, 1000) == 900);
}
