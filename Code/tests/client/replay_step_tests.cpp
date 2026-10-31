#include <catch2/catch.hpp>
#include <Games/Animation/ReplayStep.h>

TEST_CASE("An action waits one graph update after its variables", "[animation][replay-step]")
{
    AnimationReplayStep step;
    step.Phase.store(AnimationReplayStep::kVariables);
    step.Variables.Booleans = {true};
    uint32_t loads = 0, plays = 0;
    const auto load = [&](const AnimationVariables& acVariables) {
        CHECK(acVariables.Booleans == std::vector<bool>{true});
        ++loads;
    };
    const auto play = [&] { ++plays; return true; };
    RunAnimationReplayStep(step, load, play);
    CHECK(loads == 1);
    CHECK(plays == 0);
    CHECK(step.Phase.load() == AnimationReplayStep::kAction);
    RunAnimationReplayStep(step, load, play);
    CHECK(loads == 1);
    CHECK(plays == 1);
    CHECK(step.Phase.load() == AnimationReplayStep::kTaken);
    RunAnimationReplayStep(step, load, play);
    CHECK(plays == 1);
}

TEST_CASE("Refused actions get four graph attempts without reloading variables", "[animation][replay-step]")
{
    AnimationReplayStep step;
    step.Phase.store(AnimationReplayStep::kAction);
    uint32_t calls = 0;
    const auto load = [](const AnimationVariables&) { FAIL("Variables already loaded"); };
    const auto play = [&] { ++calls; return false; };
    for (size_t i = 0; i < 5; ++i) RunAnimationReplayStep(step, load, play);
    CHECK(calls == 4);
    CHECK(step.Phase.load() == AnimationReplayStep::kRefused);
}

TEST_CASE("A graph job never takes another job's busy action", "[animation][replay-step]")
{
    AnimationReplayStep step;
    step.Phase.store(AnimationReplayStep::kBusy);
    RunAnimationReplayStep(step, [](const AnimationVariables&) { FAIL("Busy"); }, [] {
        FAIL("Busy"); return false;
    });
    CHECK(step.Phase.load() == AnimationReplayStep::kBusy);
}
