// HookPerformAction's rule (animation/ActionRules.h): an owner's accepted action is recorded and sent; a copy takes
// only what we force or its half of a pair, and that is never sent.
#include <catch2/catch.hpp>

#include <animation/ActionRules.h>

using namespace ActionRules;

TEST_CASE("An owner's action is taken; a copy's own is refused", "[ActionRules]")
{
    CHECK(Before({}) == Taking{.Take = true});
    CHECK(Before({.Copy = true}) == Taking{});
    CHECK(Before({.Copy = true, .Forced = true}) == Taking{.Take = true});
    // An owner inside the player's or a replayed action is still an owner.
    CHECK(Before({.PlayerActing = true, .TargetIsThisPlayer = true, .Replaying = true}) == Taking{.Take = true});
}

TEST_CASE("A copy takes its half of this player's pair and of a replayed pair", "[ActionRules]")
{
    CHECK(Before({.Copy = true, .PlayerActing = true, .TargetIsThisPlayer = true}) == Taking{.Take = true, .PairHalf = true});
    // The player acting on someone else, or a copy targeting the player with no action of the player's under way.
    CHECK(Before({.Copy = true, .PlayerActing = true}) == Taking{});
    CHECK(Before({.Copy = true, .TargetIsThisPlayer = true}) == Taking{});
    CHECK(Before({.Copy = true, .Replaying = true}) == Taking{.Take = true, .PairHalf = true});
}

TEST_CASE("An owner's accepted action is recorded and sent, with its graph state when a copy may need it", "[ActionRules]")
{
    CHECK(After({.Captured = true, .Accepted = true, .Event = "IdleStop"}) == Outcome{.Record = true, .GraphState = true, .Send = true});
    // A pair's halves never carry it; a streamed owner's copies need it only on their own bodies.
    CHECK(After({.Captured = true, .Accepted = true, .Paired = true}) == Outcome{.Record = true, .Send = true});
    CHECK(After({.Captured = true, .Accepted = true, .Streamed = true}) == Outcome{.Record = true, .Send = true});
    CHECK(After({.Captured = true, .Accepted = true, .Streamed = true, .OwnBody = true}) == Outcome{.Record = true, .GraphState = true, .Send = true});
}

TEST_CASE("A refused action and a resolved query leave nothing", "[ActionRules]")
{
    CHECK(After({.Captured = true, .Accepted = false, .Event = "attackStart"}) == Outcome{});
    CHECK(After({.Captured = false, .Accepted = true, .Event = "attackStart"}) == Outcome{});
}

TEST_CASE("A forced action and a pair's half are recorded, never sent", "[ActionRules]")
{
    CHECK(After({.Captured = true, .Accepted = true, .Forced = true}) == Outcome{.Record = true});
    CHECK(After({.Captured = true, .Accepted = true, .PairHalf = true, .Paired = true}) == Outcome{.Record = true});
}

TEST_CASE("A new attack clears an earlier clash, whoever takes it", "[ActionRules]")
{
    CHECK(After({.Captured = true, .Accepted = true, .Event = "attackStart"}).ResetClash);
    CHECK(After({.Captured = true, .Accepted = true, .Forced = true, .Event = "pa_KillMove"}).ResetClash);
    CHECK_FALSE(After({.Captured = true, .Accepted = true, .Event = "IdleStop"}).ResetClash);
}
