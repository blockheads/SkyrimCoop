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

TEST_CASE("A copy takes its owner's oldest choice for the machine once", "[animation][start-choices]")
{
    Queue queue;
    queue.Push({"DefaultBehavior", 0, 1000});
    queue.Push({"StandingBehavior", 1, 1000});
    queue.Push({"StandingBehavior", 0, 1100});
    CHECK(queue.Take("StandingBehavior", 1200) == 1);
    CHECK(queue.Take("StandingBehavior", 1200) == 0);
    CHECK_FALSE(queue.Take("StandingBehavior", 1200));
    CHECK(queue.Take("DefaultBehavior", 1200) == 0);
}

TEST_CASE("An owner's choice its copy never reached expires", "[animation][start-choices]")
{
    Queue queue;
    queue.Push({"StandingBehavior", 1, 1000});
    CHECK_FALSE(queue.Take("StandingBehavior", 1000 + cKeepMs + 1));
    queue.Push({"StandingBehavior", 2, 5000});
    CHECK(queue.Take("StandingBehavior", 5000 + cKeepMs) == 2);
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
    CHECK(Choose(owner, false, "StandingBehavior", 1, 100) == 1);
    for (auto& choice : Unpack(Pack(owner.Drain()), 200))
        copy.Push(std::move(choice));
    CHECK(Choose(copy, true, "StandingBehavior", 0, 300) == 1); // its own variable said 0
    CHECK(Choose(copy, true, "StandingBehavior", 0, 300) == 0); // none left: the engine's
}
