#include <catch2/catch.hpp>
#include <Systems/CopyVariables.h>

#include <list>

namespace
{
struct Point
{
    uint64_t Tick{};
};

using CopyVariables::kNoAction;
using Ints = std::vector<uint32_t>;

CopyVariables::IntegerTimeline Snapshots(std::initializer_list<std::pair<uint64_t, Ints>> aEntries)
{
    CopyVariables::IntegerTimeline timeline;
    for (const auto& [cTick, cIntegers] : aEntries)
        CopyVariables::Record(timeline, cTick, cIntegers, false);
    return timeline;
}
} // namespace

TEST_CASE("A copy's graph reads the integers its owner wrote up to an interval past its time", "[copy-variables]")
{
    // The owner wrote iSyncTurnState 1 at 2020; the snapshot carrying it is stamped 2100. The copy's clip ends at 2030.
    const auto cTimeline = Snapshots({{1900, {0, 2}}, {2000, {0, 2}}, {2100, {1, 1}}, {2200, {1, 1}}});
    REQUIRE(CopyVariables::At(cTimeline, 2030, 100, kNoAction, 2));
    CHECK(*CopyVariables::At(cTimeline, 2030, 100, kNoAction, 2) == Ints{1, 1});
    // Not more than an interval past it.
    CHECK(*CopyVariables::At(cTimeline, 1999, 100, kNoAction, 2) == Ints{0, 2});
    // Only entries with as many integers.
    CHECK_FALSE(CopyVariables::At(cTimeline, 2030, 100, kNoAction, 3));
}

TEST_CASE("A copy whose clip started late reads the integers of its clip's end, not of its playback", "[copy-variables]")
{
    // Codex on PR #38: its action (1000) played when playback was 1300, so its clip ends at graph time 2000 with playback
    // at 2300. The owner wrote 1 at 2000 and, with no action, 0 at 2150: the copy's clip end reads 1.
    const auto cTimeline = Snapshots({{2000, {0}}, {2100, {1}}, {2200, {0}}, {2300, {0}}});
    CHECK(*CopyVariables::At(cTimeline, 2000, 100, kNoAction, 1) == Ints{1});
    CHECK(CopyVariables::Reference(2000.0, 2300, 400) == 2000);
}

TEST_CASE("A staged action's integers stay until its event", "[copy-variables]")
{
    // An action at 1030 carries iWantBlock 1; the snapshots around it have 0. Staged, its event late until playback is
    // past 1100: only the action's own integers are read, not a snapshot at or after it.
    auto timeline = Snapshots({{1000, {0}}, {1100, {0}}, {1200, {0}}});
    CHECK(*CopyVariables::At(timeline, 1050, 100, 1030, 1) == Ints{0}); // queued: the snapshot before it
    CopyVariables::Record(timeline, 1030, {1}, true);                    // staged
    for (const uint64_t cGraph : {1030, 1100, 1150})
        CHECK(*CopyVariables::At(timeline, cGraph, 100, 1030, 1) == Ints{1});
    // Played and popped: the owner's next snapshot within an interval.
    CHECK(*CopyVariables::At(timeline, 1030, 100, kNoAction, 1) == Ints{0});
    // An action and a snapshot of one tick: the action is after it.
    CopyVariables::Record(timeline, 1100, {7}, true);
    CHECK(*CopyVariables::At(timeline, 1100, 0, 1100, 1) == Ints{7});
}

TEST_CASE("The integer timeline records each entry once, in order, and forgets only what is read no more", "[copy-variables]")
{
    auto timeline = Snapshots({{1100, {1}}, {1000, {0}}, {1100, {9}}});
    REQUIRE(timeline.size() == 2);
    CHECK(timeline.front().Tick == 1000);
    CHECK(timeline.back().Integers == Ints{1});
    CopyVariables::Record(timeline, 1200, {2}, false);
    CopyVariables::Forget(timeline, 1150);
    REQUIRE(timeline.size() == 2); // the newest at or before 1150 stays
    CHECK(timeline.front().Tick == 1100);
    CopyVariables::Forget(timeline, 5000);
    CHECK(timeline.size() == 1);
}

TEST_CASE("A copy's graph time is anchored where its graph takes its owner's moment and runs with its updates", "[copy-variables]")
{
    CopyVariables::GraphTime graph;
    graph.Step(16.0);
    CHECK(graph.Tick.load() < 0.0); // nothing yet: playback is the reference
    CHECK(CopyVariables::Reference(graph.Tick.load(), 5000, 400) == 5000);
    graph.Anchor(1000);
    for (int update = 0; update < 60; ++update)
        graph.Step(1000.0 / 60.0);
    CHECK(graph.Tick.load() == Approx(2000.0));
    // Within kMaxMs of playback, ahead or behind: its graph's time. Further: its graph stood still, playback.
    CHECK(CopyVariables::Reference(graph.Tick.load(), 1900, 400) == 2000);
    CHECK(CopyVariables::Reference(graph.Tick.load(), 2300, 400) == 2000);
    CHECK(CopyVariables::Reference(graph.Tick.load(), 2600, 400) == 2600);
}

TEST_CASE("A copy holds the newest snapshot at or before playback, never an older one", "[copy-variables]")
{
    std::list<Point> points{{1000}, {1100}};
    CHECK(CopyVariables::Held(points, 1099).Tick == 1000);
    for (const uint64_t cPlayback : {1100, 1116, 1150, 1199})
        CHECK(CopyVariables::Held(points, cPlayback).Tick == 1100);
    points.push_back({1200});
    CHECK(CopyVariables::Held(points, 1150).Tick == 1100);
    CHECK(CopyVariables::Held(points, 1200).Tick == 1200);
}

TEST_CASE("A staged action keeps the held variables until its event", "[copy-variables]")
{
    struct Action
    {
        uint64_t Tick{};
    };
    const uint64_t cNext = CopyVariables::NextAction(std::list<Action>{{1030}});
    CHECK(cNext == 1030);
    CHECK_FALSE(CopyVariables::TakesHeld(1100, 1030, cNext));
    CHECK(CopyVariables::TakesHeld(1100, 1030, CopyVariables::NextAction(std::list<Action>{})));
    CHECK(CopyVariables::TakesHeld(1000, 900, 1030));
    CHECK_FALSE(CopyVariables::TakesHeld(1000, 1000, kNoAction));
}
