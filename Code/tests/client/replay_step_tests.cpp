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

TEST_CASE("An action waits for its clips, not forever", "[animation][replay-step][replay-clips]")
{
    AnimationReplayStep step;
    CHECK(ClipWaitOver(step, 0));

    step.ClipsLoaded = std::make_shared<std::atomic<bool>>(false);
    CHECK_FALSE(ClipWaitOver(step, 1));
    CHECK(ClipWaitOver(step, 1000));

    step.ClipsLoaded->store(true);
    CHECK(ClipWaitOver(step, 1));
}

TEST_CASE("A refused action is sent only when it changed its owner's variables", "[animation][replay-step][refused-writes]")
{
    // 39004 sets a refused TurnStop's iSyncTurnState (38949): that write is sent, with no event to replay.
    ActionEvent action;
    action.Type = 2;
    action.Variables.Integers = {0, 2};
    action.Variables.GraphRandom = 7;
    AnimationVariables after = action.Variables;
    after.GraphRandom = 9;
    CHECK_FALSE(SendRefusedVariables(action, after));
    CHECK_FALSE(VariablesOnly(action));

    after.Integers = {1, 2};
    CHECK(SendRefusedVariables(action, after));
    CHECK(VariablesOnly(action));
    CHECK((action.Type & 0x3) == 2);
    CHECK(action.Variables.Integers == std::vector<uint32_t>{1, 2});
    CHECK(action.Variables.GraphRandom == 7); // the seed as the action started
}

TEST_CASE("A copy loads a refused action's variables in step and sends no event", "[animation][replay-step][refused-writes]")
{
    AnimationReplayStep step;
    step.Action.Type = 2 | kVariablesOnly;
    step.Variables.Integers = {1};
    step.Phase.store(AnimationReplayStep::kVariables);
    uint32_t loads = 0;
    const auto load = [&](const AnimationVariables& acVariables) {
        CHECK(acVariables.Integers == std::vector<uint32_t>{1});
        ++loads;
    };
    const auto play = [] { FAIL("A refused action's event is never replayed"); return false; };
    RunAnimationReplayStep(step, load, play);
    CHECK(loads == 1);
    CHECK(step.Phase.load() == AnimationReplayStep::kAction);
    RunAnimationReplayStep(step, load, play);
    CHECK(step.Phase.load() == AnimationReplayStep::kTaken);
}
