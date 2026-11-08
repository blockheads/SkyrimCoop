#include <catch2/catch.hpp>
#include <Systems/CopyVariables.h>

#include <list>

namespace
{
struct Point
{
    uint64_t Tick{};
    AnimationVariables Variables{};
};

AnimationVariables Integers(std::vector<uint32_t> aValues)
{
    AnimationVariables variables;
    variables.Integers = std::move(aValues);
    return variables;
}

using CopyVariables::kNoAction;
} // namespace

TEST_CASE("A copy takes the next snapshot's integers within a send interval of its graph", "[copy-variables]")
{
    // The owner wrote iSyncTurnState 1 at 1020; the snapshot carrying it is stamped 1100. The copy's clip ends at 1030.
    const std::list<Point> cPoints{{1000, Integers({0, 2})}, {1100, Integers({1, 1})}};
    const auto cShown = Integers({0, 2});
    const auto* pAhead = CopyVariables::IntegersAhead(cPoints, cShown, 1000, 1030, 100, kNoAction);
    REQUIRE(pAhead);
    CHECK(pAhead->Integers == std::vector<uint32_t>{1, 1});
    // Not more than an interval past the graph's time.
    CHECK_FALSE(CopyVariables::IntegersAhead(cPoints, cShown, 1000, 999, 100, kNoAction));
    // Not another layout.
    CHECK_FALSE(CopyVariables::IntegersAhead(std::list<Point>{{1000, Integers({0, 2})}, {1100, Integers({1})}}, cShown, 1000, 1030, 100, kNoAction));
}

TEST_CASE("A copy takes no integers at or past an action not yet played", "[copy-variables]")
{
    // An action at 1030 carries iWantBlock 1 and is staged: its variables are in, its event not yet sent. The next
    // snapshot (1100) has 0: taken now, it would be in the graph when the event goes.
    const std::list<Point> cPoints{{1000, Integers({0})}, {1100, Integers({0})}};
    const auto cAction = Integers({1});
    CHECK_FALSE(CopyVariables::IntegersAhead(cPoints, cAction, 1030, 1046, 100, 1030));
    // Queued, not yet staged: the snapshot after it waits for it too.
    CHECK_FALSE(CopyVariables::IntegersAhead(cPoints, Integers({2}), 1000, 1020, 100, 1050));
    // Played: the snapshot after it is the owner's next.
    CHECK(CopyVariables::IntegersAhead(cPoints, cAction, 1030, 1046, 100, kNoAction));
    // Nothing older than what an action already loaded.
    CHECK_FALSE(CopyVariables::IntegersAhead(cPoints, cAction, 1100, 1046, 100, kNoAction));
}

TEST_CASE("A copy's integers never go back to an older snapshot", "[copy-variables]")
{
    // Only {1000: 0, 1100: 1} arrived. Before 1100 the copy shows 1 ahead; at 1100 and while it extrapolates past the
    // last snapshot, it holds the 1100 one, not the first.
    std::list<Point> points{{1000, Integers({0})}, {1100, Integers({1})}};
    CHECK(CopyVariables::Held(points, 1099).Tick == 1000);
    CHECK(CopyVariables::IntegersAhead(points, Integers({0}), 1000, 1099, 100, kNoAction)->Integers == std::vector<uint32_t>{1});
    for (const uint64_t cPlayback : {1100, 1116, 1150, 1199})
        CHECK(CopyVariables::Held(points, cPlayback).Tick == 1100);
    // A third arrives: still nothing older.
    points.push_back({1200, Integers({1})});
    CHECK(CopyVariables::Held(points, 1150).Tick == 1100);
    CHECK(CopyVariables::Held(points, 1200).Tick == 1200);
}

TEST_CASE("A copy's graph time runs on its frames, within playback and a cap ahead", "[copy-variables]")
{
    CopyVariables::GraphClock graph;
    graph.ActionTaken(1000);
    // Playback slowed to 0.9x as its send interval steered from 33 to 100 ms; its clips run at 1x.
    double playback = 1000.0;
    uint64_t tick = 0;
    for (int frame = 0; frame < 60; ++frame)
    {
        playback += 0.9 * 1000.0 / 60.0;
        tick = graph.Advance(uint64_t(playback), 1000.0 / 60.0, 250);
    }
    CHECK(tick == Approx(2000).margin(1));
    CHECK(uint64_t(playback) < tick - 90);
    // Never behind playback, never more than the cap ahead.
    CHECK(graph.Advance(5000, 16.0, 250) == 5000);
    CHECK(graph.Advance(5000, 1000.0, 250) == 5250);
    // An action taken re-anchors it, playback holding it from below.
    graph.ActionTaken(4900);
    CHECK(graph.Advance(5000, 16.0, 250) == 5000);
}
