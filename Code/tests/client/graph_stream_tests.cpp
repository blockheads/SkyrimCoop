#include <Games/Animation/GraphStream.h>

#include <catch2/catch.hpp>

#include <bit>

namespace
{
// A graph whose state is its inputs folded together: equal inputs from equal starts, equal states.
struct ToyGraph final : IGraphTarget
{
    std::vector<uint32_t> Vars = std::vector<uint32_t>(4, 0);
    GraphSeed Seed{};
    uint64_t State = 0;
    std::vector<std::string> Calls;
    bool OwnChannel = false; // a copy's engine writes its own channel (63590) before each update

    std::vector<uint32_t> Words() override
    {
        return Vars;
    }
    void WriteWords(std::span<const uint32_t> aWords) override
    {
        Vars.assign(aWords.begin(), aWords.end());
    }
    void SetSeed(const GraphSeed& acSeed) override
    {
        Seed = acSeed;
    }
    bool LoadPoint(const Payloads::GraphSyncPoint& acPoint) override
    {
        State = std::stoull(acPoint.State);
        Seed = acPoint.Seed;
        Calls.push_back("load");
        return true;
    }
    void SendEvent(uint16_t aEventId) override
    {
        State = State * 31 + aEventId + Vars[0];
        Calls.push_back("event " + std::to_string(aEventId));
    }
    std::shared_ptr<std::atomic<bool>> Clips; // a load's files loading
    std::vector<std::pair<uint8_t, uint16_t>> Asked;
    std::shared_ptr<std::atomic<bool>> RequestClips(uint8_t aSet, uint16_t aEventId) override
    {
        Asked.emplace_back(aSet, aEventId);
        return Clips;
    }
    void Update(float aStep) override
    {
        State = State * 17 + std::bit_cast<uint32_t>(aStep) + Vars[0] + Vars[1] + Vars[3] + Seed.Current;
        Vars[2] = uint32_t(State); // a variable the graph writes itself
        Calls.push_back("update");
    }
    void Generate() override
    {
        State = State * 13 + 5; // its modifiers move
        Calls.push_back("generate");
    }
};

// The owner's side of one update: inputs recorded, the update run, what it wrote noted.
void OwnerUpdate(GraphRecorder& aRecorder, ToyGraph& aGraph, uint64_t aTick, float aStep)
{
    aRecorder.Update(aTick, aStep, aGraph.Vars, aGraph.Seed);
    aGraph.Update(aStep);
    aRecorder.Updated(aGraph.Vars);
}

GraphOps Batch(GraphRecorder& aRecorder, uint32_t aActor = 1)
{
    GraphRecordedRun run{aActor, 0, {}};
    run.Ops = aRecorder.Take(run.FirstUpdate);
    return BatchGraphOps({run});
}

Payloads::GraphSyncPoint Point(const ToyGraph& acGraph, uint32_t aUpdates)
{
    Payloads::GraphSyncPoint point;
    point.Update = aUpdates;
    point.State = std::to_string(acGraph.State);
    point.Words = acGraph.Vars;
    point.Seed = acGraph.Seed;
    return point;
}
} // namespace

TEST_CASE("An op carries only the variables set from outside the graph", "[graph-stream]")
{
    GraphRecorder recorder;
    ToyGraph graph;
    OwnerUpdate(recorder, graph, 1, 0.016f);
    (void)recorder.Take(*std::make_unique<uint32_t>());
    graph.Vars[0] = 5; // a channel
    OwnerUpdate(recorder, graph, 2, 0.016f);
    OwnerUpdate(recorder, graph, 3, 0.016f); // nothing set from outside: its own Vars[2] is not sent
    uint32_t first = 0;
    const auto cOps = recorder.Take(first);
    CHECK(first == 1);
    REQUIRE(cOps.size() == 2);
    CHECK(cOps[0].Op.Changed == std::vector<std::pair<uint16_t, uint32_t>>{{0, 5}});
    CHECK(cOps[1].Op.Changed.empty());
}

TEST_CASE("TimeDelta is left out when it is the step", "[graph-stream]")
{
    GraphRecorder recorder;
    recorder.TimeDelta = 3;
    ToyGraph graph;
    graph.Vars[3] = std::bit_cast<uint32_t>(0.016f);
    OwnerUpdate(recorder, graph, 1, 0.016f);
    graph.Vars[3] = std::bit_cast<uint32_t>(0.5f); // something else wrote it
    OwnerUpdate(recorder, graph, 2, 0.017f);
    uint32_t first = 0;
    const auto cOps = recorder.Take(first);
    CHECK(cOps[0].Op.Changed ==
          std::vector<std::pair<uint16_t, uint32_t>>{{0, 0}, {1, 0}, {2, 0}}); // the first: all but TimeDelta
    CHECK(cOps[1].Op.Changed.back() == std::pair<uint16_t, uint32_t>{3, std::bit_cast<uint32_t>(0.5f)});
}

TEST_CASE("A far owner's chunked steps keep its own TimeDelta on its copy", "[graph-stream]")
{
    // 63587 writes the frame's time; a far graph steps a whole interval, or nothing, at once (58329).
    GraphRecorder recorder;
    recorder.TimeDelta = 3;
    ToyGraph owner, copy;
    const uint32_t cFrame = std::bit_cast<uint32_t>(0.016f);
    owner.Vars[3] = cFrame;
    OwnerUpdate(recorder, owner, 1, 0.016f);
    GraphReplayer replayer;
    replayer.TimeDelta = 3;
    replayer.Load(Point(owner, recorder.Updates()));
    std::vector<uint64_t> states;
    const float cSteps[] = {0.25f, 0.f, 0.f, 0.25f};
    for (uint64_t tick = 2; tick <= 5; ++tick)
    {
        owner.Vars[3] = cFrame;
        OwnerUpdate(recorder, owner, tick, cSteps[tick - 2]);
        states.push_back(owner.State);
    }
    const GraphOps cOps = Batch(recorder);
    replayer.Feed(cOps.Runs[0], cOps.Frames);
    CHECK(replayer.Run(5, copy) == 4);
    CHECK(copy.State == states.back());
    CHECK(copy.Vars[3] == cFrame);
}

TEST_CASE("A copy runs its owner's generate passes in their place, with their updates", "[graph-stream]")
{
    // A graph generates after each frame it updated in (63589 -> 63568): a far owner's only after its chunks.
    GraphRecorder recorder;
    ToyGraph owner, copy;
    OwnerUpdate(recorder, owner, 1, 0.016f);
    recorder.Generate(1);
    owner.Generate();
    GraphReplayer replayer;
    replayer.Load(Point(owner, recorder.Updates())); // as update 1 begins: the generate before it is in it
    const GraphOps cBefore = Batch(recorder);
    for (uint64_t tick = 2; tick <= 4; ++tick)
    {
        OwnerUpdate(recorder, owner, tick, tick == 3 ? 0.25f : 0.f);
        if (tick == 3)
        {
            recorder.Generate(tick);
            owner.Generate();
        }
    }
    const GraphOps cAfter = Batch(recorder);
    replayer.Feed(cBefore.Runs[0], cBefore.Frames);
    replayer.Feed(cAfter.Runs[0], cAfter.Frames);
    CHECK(replayer.Run(3, copy) == 2); // the generate after update 3 is due with it, not with update 4
    CHECK(copy.Calls == std::vector<std::string>{"load", "update", "update", "generate"});
    CHECK(replayer.Run(4, copy) == 1);
    CHECK(copy.State == owner.State);
}

TEST_CASE("A copy catching up never ends a frame between an update and its generate pass", "[graph-stream]")
{
    GraphRecorder recorder;
    ToyGraph owner, copy;
    GraphReplayer replayer;
    replayer.Load(Point(owner, 0));
    for (uint64_t tick = 1; tick <= GraphReplayer::cMaxUpdatesPerRun + 1; ++tick)
    {
        OwnerUpdate(recorder, owner, tick, 0.016f);
        recorder.Generate(tick);
        owner.Generate();
    }
    const GraphOps cOps = Batch(recorder);
    replayer.Feed(cOps.Runs[0], cOps.Frames);
    CHECK(replayer.Run(100, copy) == GraphReplayer::cMaxUpdatesPerRun);
    CHECK(copy.Calls.back() == "generate");
    CHECK(replayer.Run(100, copy) == 1);
    CHECK(copy.State == owner.State);
}

TEST_CASE("A copy whose graph is another project's drops its owner's point and asks again with a new graph", "[graph-stream]")
{
    // Skyrim (duo, Riverwood, 2026-11-09): the host's leveled 105A25 was a wolf, the guest's reference still a hare.
    // The wolf's point went into the hare's graph (nodes matched by id), and the copy parted at once.
    GraphStreams streams;
    GraphOpsRun run{1, 0, {{.Frame = 0}}};
    streams.Feed(0x99, run, {{1, 0.016f}});
    CHECK(streams.ShouldAsk(0x99));
    ToyGraph hare, wolf;
    Payloads::GraphSyncPoint point = Point(wolf, 0);
    point.Project = 7;
    streams.Load(0x99, point);
    CHECK(streams.Replay(0x99, hare, &hare, 8) == 0);
    CHECK(hare.Calls.empty());
    CHECK_FALSE(streams.OnStream(0x99));
    CHECK_FALSE(streams.ShouldAsk(0x99)); // nothing fits this graph
    streams.SeeGraph(0x99, &hare);
    CHECK_FALSE(streams.ShouldAsk(0x99));
    streams.SeeGraph(0x99, &wolf); // its 3D reloaded with its owner's base: off its stream, nothing replays
    CHECK(streams.ShouldAsk(0x99));
    streams.Load(0x99, point);
    streams.SetPlayback(0x99, 100);
    CHECK(streams.Replay(0x99, wolf, &wolf, 7) == 1);
    CHECK(streams.OnStream(0x99));
}

TEST_CASE("Actors' updates in one frame share its step", "[graph-stream]")
{
    GraphRecorder a, b;
    ToyGraph ga, gb;
    for (uint64_t tick = 1; tick <= 3; ++tick)
    {
        OwnerUpdate(a, ga, tick, 0.016f);
        OwnerUpdate(b, gb, tick, 0.016f);
    }
    GraphRecordedRun ra{1, 0, {}}, rb{2, 0, {}};
    ra.Ops = a.Take(ra.FirstUpdate);
    rb.Ops = b.Take(rb.FirstUpdate);
    const GraphOps cOps = BatchGraphOps({ra, rb});
    CHECK(cOps.Frames.size() == 3);
    CHECK(cOps.Runs[1].Ops[2].Frame == 2);
}

TEST_CASE("A copy fed its owner's ops from a sync point matches it", "[graph-stream]")
{
    GraphRecorder recorder;
    ToyGraph owner, copy;
    copy.OwnChannel = true;
    OwnerUpdate(recorder, owner, 1, 0.016f);
    OwnerUpdate(recorder, owner, 2, 0.016f);
    const auto cPoint = Point(owner, recorder.Updates());
    const GraphOps cBefore = Batch(recorder); // before the point: dropped
    std::vector<uint64_t> states;
    for (uint64_t tick = 3; tick <= 8; ++tick)
    {
        owner.Vars[0] = uint32_t(tick * 3);
        if (tick == 5)
        {
            recorder.Event(tick, 7, owner.Vars, owner.Seed);
            owner.SendEvent(7);
        }
        if (tick == 6)
            owner.Seed = {9, 10};
        OwnerUpdate(recorder, owner, tick, 0.016f + 0.001f * float(tick));
        states.push_back(owner.State);
    }
    const GraphOps cAfter = Batch(recorder);

    GraphReplayer replayer;
    replayer.Feed(cBefore.Runs[0], cBefore.Frames);
    CHECK(replayer.NeedsPoint());
    replayer.Feed(cAfter.Runs[0], cAfter.Frames);
    replayer.Load(cPoint);
    copy.Vars[0] = 999;                // its own channel: kept out
    CHECK(replayer.Run(4, copy) == 2); // due by playback tick 4: updates 3 and 4
    CHECK(copy.State == states[1]);
    copy.Vars[0] = 999;
    CHECK(replayer.Run(100, copy) == GraphReplayer::cMaxUpdatesPerRun); // behind: a few a frame
    CHECK(copy.State == states[5]);
    CHECK(copy.Calls ==
          std::vector<std::string>{"load", "update", "update", "event 7", "update", "update", "update", "update"});
}

TEST_CASE("A gap in a copy's stream waits for a new sync point", "[graph-stream]")
{
    GraphRecorder recorder;
    ToyGraph owner, copy;
    OwnerUpdate(recorder, owner, 1, 0.016f);
    GraphReplayer replayer;
    replayer.Load(Point(owner, recorder.Updates()));
    OwnerUpdate(recorder, owner, 2, 0.016f);
    const GraphOps cFirst = Batch(recorder);
    replayer.Feed(cFirst.Runs[0], cFirst.Frames);
    CHECK(replayer.Run(2, copy) == 1);
    OwnerUpdate(recorder, owner, 3, 0.016f);
    (void)Batch(recorder); // lost
    OwnerUpdate(recorder, owner, 4, 0.016f);
    const GraphOps cThird = Batch(recorder);
    replayer.Feed(cThird.Runs[0], cThird.Frames);
    CHECK(replayer.NeedsPoint());
    CHECK(replayer.Run(4, copy) == 0);
}

TEST_CASE("A copy's game asks for a sync point once, and again after a gap", "[graph-stream]")
{
    GraphStreams streams;
    GraphOpsRun run{1, 0, {{.Frame = 0}}};
    streams.Feed(0x99, run, {{1, 0.016f}});
    CHECK(streams.ShouldAsk(0x99));
    CHECK_FALSE(streams.ShouldAsk(0x99));
    ToyGraph copy;
    Payloads::GraphSyncPoint point;
    point.State = "0";
    point.Update = 1;
    streams.Load(0x99, point);
    streams.SetPlayback(0x99, 5);
    (void)streams.Replay(0x99, copy);
    CHECK(streams.OnStream(0x99));
    streams.Feed(0x99, {1, 5, {{.Frame = 0}}}, {{6, 0.016f}}); // update 1 to 4 never came
    CHECK_FALSE(streams.OnStream(0x99));
    CHECK(streams.ShouldAsk(0x99));
}

TEST_CASE("An owner's sync point is taken as the update after a copy asked begins", "[graph-stream]")
{
    GraphStreams streams;
    streams.Record(0x50, 7);
    ToyGraph graph;
    CHECK_FALSE(streams.PointWanted(0x50));
    streams.OnUpdate(0x50, 1, 0.016f, graph.Vars, graph.Seed);
    streams.OnUpdated(0x50, graph.Vars);
    streams.WantPoint(0x50);
    REQUIRE(streams.PointWanted(0x50)); // as update 1 (its second) begins
    CHECK_FALSE(streams.PointWanted(0x50));
    streams.AddPoint(0x50, Point(graph, 0));
    streams.OnUpdate(0x50, 2, 0.016f, graph.Vars, graph.Seed);
    streams.OnUpdated(0x50, graph.Vars);
    const auto cPoints = streams.TakePoints();
    REQUIRE(cPoints.size() == 1);
    CHECK(cPoints[0].Id == 7);
    CHECK(cPoints[0].Update == 1); // the update its copy replays first
    const auto cOps = streams.TakeOps();
    REQUIRE(cOps);
    CHECK(cOps->Runs[0].ActorId == 7);
    CHECK(cOps->Runs[0].Ops.size() == 2);
}

TEST_CASE("Its owner's clip load holds what follows until its files are in; an event alone loads nothing", "[graph-stream]")
{
    GraphRecorder recorder;
    ToyGraph owner, copy;
    OwnerUpdate(recorder, owner, 1, 0.016f);
    GraphReplayer replayer;
    replayer.Load(Point(owner, recorder.Updates())); // as its second update begins
    OwnerUpdate(recorder, owner, 2, 0.016f);
    recorder.Clips(2, 4, 7); // its process loaded event 7's clips into its instant interaction set, then sent it
    recorder.Event(2, 7, owner.Vars, owner.Seed);
    owner.SendEvent(7);
    OwnerUpdate(recorder, owner, 3, 0.016f);
    const GraphOps cOps = Batch(recorder);
    replayer.Feed(cOps.Runs[0], cOps.Frames);
    copy.Clips = std::make_shared<std::atomic<bool>>(false);
    CHECK(replayer.Run(10, copy) == 1); // its point's update; then its files are not in: the event and what follows wait
    CHECK(copy.Asked == std::vector<std::pair<uint8_t, uint16_t>>{{4, 7}});
    CHECK(copy.Calls == std::vector<std::string>{"load", "update"});
    copy.Clips->store(true);
    CHECK(replayer.Run(10, copy) == 1);
    CHECK(copy.State == owner.State);
    CHECK(copy.Asked.size() == 1);

    // An event with no load before it asks for nothing (its owner's process loaded nothing for it).
    recorder.Event(3, 8, owner.Vars, owner.Seed);
    owner.SendEvent(8);
    OwnerUpdate(recorder, owner, 3, 0.016f);
    const GraphOps cMore = Batch(recorder);
    replayer.Feed(cMore.Runs[0], cMore.Frames);
    CHECK(replayer.Run(10, copy) == 1);
    CHECK(copy.Asked.size() == 1);

    // A load that never ends (a cancelled one): it goes on after cMaxClipWaits runs.
    recorder.Clips(4, 3, 9);
    OwnerUpdate(recorder, owner, 4, 0.016f);
    const GraphOps cLast = Batch(recorder);
    replayer.Feed(cLast.Runs[0], cLast.Frames);
    copy.Clips = std::make_shared<std::atomic<bool>>(false);
    for (uint32_t i = 0; i < GraphReplayer::cMaxClipWaits; ++i)
        CHECK(replayer.Run(10, copy) == 0);
    CHECK(replayer.Run(10, copy) == 1);
    CHECK(copy.State == owner.State);
}

TEST_CASE("A sync point loads every clip set its owner's process held before it loads", "[graph-stream]")
{
    GraphRecorder recorder;
    ToyGraph owner, copy;
    recorder.Event(1, 42, owner.Vars, owner.Seed);
    owner.SendEvent(42);
    OwnerUpdate(recorder, owner, 1, 0.016f);
    auto point = Point(owner, recorder.Updates());
    point.ClipSets = {{3, 42}, {4, 9}}; // its action's and its instant interaction's
    OwnerUpdate(recorder, owner, 2, 0.016f);
    const GraphOps cOps = Batch(recorder);
    GraphReplayer replayer;
    replayer.Feed(cOps.Runs[0], cOps.Frames);
    replayer.Load(point);
    copy.Clips = std::make_shared<std::atomic<bool>>(false);
    CHECK(replayer.Run(10, copy) == 0); // holding for the files, its point not loaded yet: a clip binds its file as it activates
    CHECK(copy.Asked == std::vector<std::pair<uint8_t, uint16_t>>{{3, 42}, {4, 9}});
    CHECK(copy.Calls.empty());
    CHECK(replayer.Run(10, copy) == 0);
    CHECK(copy.Asked.size() == 2); // asked once
    copy.Clips->store(true);
    CHECK(replayer.Run(10, copy) == 1);
    CHECK(copy.Calls.front() == "load");
    CHECK(copy.State == owner.State);
}

TEST_CASE("A copy's stream starts again when its graph is replaced", "[graph-stream]")
{
    GraphStreams streams;
    ToyGraph copy;
    streams.Feed(0x99, {1, 0, {{.Frame = 0}}}, {{1, 0.016f}});
    (void)streams.ShouldAsk(0x99);
    Payloads::GraphSyncPoint point;
    point.State = "0";
    point.Update = 1;
    streams.Load(0x99, point);
    int first = 0, second = 0;
    (void)streams.Replay(0x99, copy, &first);
    CHECK(streams.OnStream(0x99));
    (void)streams.Replay(0x99, copy, &second); // another graph
    CHECK_FALSE(streams.OnStream(0x99));
    CHECK(streams.ShouldAsk(0x99));
}

TEST_CASE("An owner's sync point keeps the clip sets its game gave it, whatever the graph's last event", "[graph-stream]")
{
    GraphStreams streams;
    streams.Record(0x42, 7);
    const GraphSeed cSeed{1, 1};
    const std::vector<uint32_t> cWords{0, 0};
    streams.OnEvent(0x42, 1, 9, cWords, cSeed); // IdleStop, refused: no files of its own
    streams.OnUpdate(0x42, 1, 0.016f, cWords, cSeed);
    streams.AddPoint(0x42, {.ClipSets = {{3, 3}}});
    const auto cPoints = streams.TakePoints();
    REQUIRE(cPoints.size() == 1);
    CHECK(cPoints[0].ClipSets == std::vector<std::pair<uint8_t, uint16_t>>{{3, 3}});
    CHECK(cPoints[0].Id == 7);
}

TEST_CASE("An owner's clip loads go into its stream in their place", "[graph-stream]")
{
    GraphStreams streams;
    streams.Record(0x42, 7);
    const GraphSeed cSeed{1, 1};
    const std::vector<uint32_t> cWords{0, 0};
    streams.OnUpdate(0x42, 1, 0.016f, cWords, cSeed);
    streams.OnClips(0x42, 2, 4, 11);
    streams.OnEvent(0x42, 2, 11, cWords, cSeed);
    streams.OnClips(0x99, 2, 3, 1); // not an owner's: nothing
    const auto cOps = streams.TakeOps();
    REQUIRE(cOps);
    REQUIRE(cOps->Runs.size() == 1);
    const auto& cRun = cOps->Runs[0].Ops;
    REQUIRE(cRun.size() == 3);
    CHECK((cRun[1].Event && cRun[1].ClipSet == 4 && cRun[1].EventId == 11 && cRun[1].Changed.empty()));
    CHECK((cRun[2].Event && cRun[2].ClipSet == 0 && cRun[2].EventId == 11));
}

TEST_CASE("A sync point taken as an update begins holds the events just before it: they are not replayed", "[graph-stream]")
{
    GraphRecorder recorder;
    ToyGraph owner, copy;
    OwnerUpdate(recorder, owner, 1, 0.016f);
    recorder.Event(2, 7, owner.Vars, owner.Seed); // handled by its owner's graph as it came
    owner.SendEvent(7);
    const auto cPoint = Point(owner, recorder.Updates()); // then its next update begins: the point has the event in it
    OwnerUpdate(recorder, owner, 2, 0.016f);
    const GraphOps cOps = Batch(recorder);
    GraphReplayer replayer;
    replayer.Feed(cOps.Runs[0], cOps.Frames);
    replayer.Load(cPoint);
    CHECK(replayer.Run(10, copy) == 1);
    CHECK(copy.Calls == std::vector<std::string>{"load", "update"});
    CHECK(copy.State == owner.State);
}

TEST_CASE("An owner in a pair is not recorded, gives no point, and starts over after", "[graph-stream]")
{
    // Skyrim (duo, host-kill-move, 2026-11-09): a pair's clip runs on its instance (32803: the wait at the mark), which
    // no stream carries; a copy replaying the owner's inputs through it ran 150 ms ahead of its owner.
    GraphStreams streams;
    streams.Record(0x50, 7);
    ToyGraph graph;
    streams.OnUpdate(0x50, 1, 0.016f, graph.Vars, graph.Seed);
    streams.OnUpdated(0x50, graph.Vars);
    (void)streams.TakeOps();
    CHECK(streams.PauseWhile(0x50, true));
    streams.OnEvent(0x50, 2, 3, graph.Vars, graph.Seed);
    streams.OnUpdate(0x50, 2, 0.016f, graph.Vars, graph.Seed);
    streams.OnUpdated(0x50, graph.Vars);
    streams.OnGenerate(0x50, 2);
    CHECK_FALSE(streams.TakeOps());
    streams.WantPoint(0x50);
    CHECK_FALSE(streams.PointWanted(0x50)); // kept for after the pair
    CHECK_FALSE(streams.PauseWhile(0x50, false));
    CHECK(streams.PointWanted(0x50));
    streams.OnUpdate(0x50, 3, 0.016f, graph.Vars, graph.Seed);
    const auto cOps = streams.TakeOps();
    REQUIRE(cOps);
    CHECK(cOps->Runs[0].FirstUpdate == 0); // its recording started over: its copies see the gap
}

TEST_CASE("A copy held for its owner's pair is back on its stream from a point after the pair, not one before", "[graph-stream]")
{
    GraphStreams streams;
    streams.Feed(0x99, {1, 0, {{.Frame = 0}}}, {{1, 0.016f}});
    ToyGraph copy;
    Payloads::GraphSyncPoint point = Point(copy, 1);
    point.Tick = 90;
    streams.Load(0x99, point);
    streams.SetPlayback(0x99, 5);
    (void)streams.Replay(0x99, copy);
    REQUIRE(streams.OnStream(0x99));
    (void)streams.ShouldAsk(0x99);

    streams.HoldUntilPoint(0x99, 100); // its owner's half of the pair, at tick 100
    CHECK(streams.Held(0x99));
    CHECK_FALSE(streams.OnStream(0x99));
    CHECK(streams.HoldWhile(0x99, false)); // it is in no instance here: still held
    CHECK(streams.ShouldAsk(0x99));       // its owner answers once its pair is over
    CHECK_FALSE(streams.ShouldAsk(0x99));
    streams.Load(0x99, point); // asked for before the pair
    CHECK(streams.Held(0x99));
    CHECK(streams.ShouldAsk(0x99));
    point.Tick = 2000;
    streams.Load(0x99, point);
    CHECK_FALSE(streams.Held(0x99));
    CHECK(streams.OnStream(0x99));
}
