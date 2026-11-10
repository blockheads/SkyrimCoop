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

TEST_CASE("A copy's next action comes as far in its graph's time as on its owner's", "[animation][replay-step][action-spacing]")
{
    // Turn at owner tick 10000, its turnStop 960 ms later; the copy played the turn at graph time 5 s.
    const ActionAnchor cTurn{10000, 5'000'000, true};
    // Its playback steered to 0.9x: 900 ms on, playback reads 10900, but its graph ran 960 ms.
    CHECK(ActionDue(cTurn, 10960, 5'960'000, 10900));
    CHECK_FALSE(ActionDue(cTurn, 10960, 5'950'000, 10900));
    // Never more than 250 ms before playback.
    CHECK_FALSE(ActionDue(cTurn, 10960, 5'960'000, 10700));
    // A graph standing still: due 250 ms after playback.
    CHECK_FALSE(ActionDue(cTurn, 10960, 5'000'000, 11200));
    CHECK(ActionDue(cTurn, 10960, 5'000'000, 11210));
}

TEST_CASE("A copy's first action comes at playback", "[animation][replay-step][action-spacing]")
{
    CHECK_FALSE(ActionDue({}, 10000, 0, 9999));
    CHECK(ActionDue({}, 10000, 0, 10000));
    // A paired action: at playback.
    CHECK_FALSE(ActionDue({10000, 1'000'000, true}, 10500, 2'000'000, 10400, true));
    CHECK(ActionDue({10000, 1'000'000, true}, 10500, 2'000'000, 10500, true));
    // Ticks out of order (an older action after a newer one): no negative gap.
    CHECK(ActionDue({10000, 1'000'000, true}, 9990, 1'000'000, 9800));
}

TEST_CASE("A pair or a death in a copy's queue comes at playback, with the actions before it", "[animation][replay-step][action-spacing]")
{
    ActionEvent move, pair, death;
    move.Tick = 10480;
    pair.Tick = 10500;
    pair.TargetEventName = "KillMoveI";
    death.Tick = 10500;
    death.State1 = DeathActions::kDying;
    REQUIRE(DeathActions::IsDying(death.State1));
    const ActionAnchor cAnchor{10000, 1'000'000, true};
    // The move alone waits on its graph's clock.
    CHECK_FALSE(AtPlayback(std::vector<ActionEvent>{move}, 10500));
    CHECK_FALSE(ActionDue(cAnchor, move.Tick, 1'200'000, 10500, false));
    for (const auto& cLater : {pair, death})
    {
        // At the front it comes at playback, never before on the graph's clock.
        CHECK(AtPlayback(std::vector<ActionEvent>{cLater}, 10400));
        CHECK_FALSE(ActionDue(cAnchor, cLater.Tick, 2'000'000, 10400, true));
        CHECK(ActionDue(cAnchor, cLater.Tick, 1'000'000, 10500, true));
        // Behind the move: once due it takes the move along.
        const std::vector<ActionEvent> cQueue{move, cLater};
        CHECK_FALSE(AtPlayback(cQueue, 10499));
        CHECK(AtPlayback(cQueue, 10500));
        CHECK(ActionDue(cAnchor, move.Tick, 1'200'000, 10500, AtPlayback(cQueue, 10500)));
    }
}
