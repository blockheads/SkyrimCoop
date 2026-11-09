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
    std::shared_ptr<std::atomic<bool>> Clips; // an event's files loading
    std::shared_ptr<std::atomic<bool>> RequestClips(uint16_t) override { return Clips; }
    void Update(float aStep) override
    {
        State = State * 17 + std::bit_cast<uint32_t>(aStep) + Vars[0] + Vars[1] + Seed.Current;
        Vars[2] = uint32_t(State); // a variable the graph writes itself
        Calls.push_back("update");
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

TEST_CASE("An owner's sync point is taken after the update a copy asked at", "[graph-stream]")
{
    GraphStreams streams;
    streams.Record(0x50, 7);
    ToyGraph graph;
    streams.OnUpdate(0x50, 1, 0.016f, graph.Vars, graph.Seed);
    CHECK_FALSE(streams.OnUpdated(0x50, graph.Vars));
    streams.WantPoint(0x50);
    streams.OnUpdate(0x50, 2, 0.016f, graph.Vars, graph.Seed);
    REQUIRE(streams.OnUpdated(0x50, graph.Vars));
    streams.AddPoint(0x50, Point(graph, 0));
    const auto cPoints = streams.TakePoints();
    REQUIRE(cPoints.size() == 1);
    CHECK(cPoints[0].Id == 7);
    CHECK(cPoints[0].Update == 2);
    const auto cOps = streams.TakeOps();
    REQUIRE(cOps);
    CHECK(cOps->Runs[0].ActorId == 7);
    CHECK(cOps->Runs[0].Ops.size() == 2);
}

TEST_CASE("An event waits for its clip files, holding what follows", "[graph-stream]")
{
    GraphRecorder recorder;
    ToyGraph owner, copy;
    OwnerUpdate(recorder, owner, 1, 0.016f);
    GraphReplayer replayer;
    replayer.Load(Point(owner, recorder.Updates()));
    recorder.Event(2, 7, owner.Vars, owner.Seed);
    owner.SendEvent(7);
    OwnerUpdate(recorder, owner, 2, 0.016f);
    const GraphOps cOps = Batch(recorder);
    replayer.Feed(cOps.Runs[0], cOps.Frames);
    copy.Clips = std::make_shared<std::atomic<bool>>(false);
    CHECK(replayer.Run(10, copy) == 0); // its files are not in: the event and the update after it wait
    CHECK(copy.Calls == std::vector<std::string>{"load"});
    copy.Clips->store(true);
    CHECK(replayer.Run(10, copy) == 1);
    CHECK(copy.State == owner.State);

    // A load that never ends (a cancelled one): it goes on after cMaxClipWaits runs.
    recorder.Event(3, 8, owner.Vars, owner.Seed);
    owner.SendEvent(8);
    OwnerUpdate(recorder, owner, 3, 0.016f);
    const GraphOps cMore = Batch(recorder);
    replayer.Feed(cMore.Runs[0], cMore.Frames);
    copy.Clips = std::make_shared<std::atomic<bool>>(false);
    for (uint32_t i = 0; i < GraphReplayer::cMaxClipWaits; ++i)
        CHECK(replayer.Run(10, copy) == 0);
    CHECK(replayer.Run(10, copy) == 1);
}

TEST_CASE("A sync point loads its owner's last action's clip files before the stream runs on", "[graph-stream]")
{
    GraphRecorder recorder;
    ToyGraph owner, copy;
    recorder.Event(1, 42, owner.Vars, owner.Seed); // its owner's last action: its process holds that set
    owner.SendEvent(42);
    OwnerUpdate(recorder, owner, 1, 0.016f);
    auto point = Point(owner, recorder.Updates());
    point.ClipEvent = 42;
    OwnerUpdate(recorder, owner, 2, 0.016f);
    const GraphOps cOps = Batch(recorder);
    GraphReplayer replayer;
    replayer.Feed(cOps.Runs[0], cOps.Frames);
    replayer.Load(point);
    copy.Clips = std::make_shared<std::atomic<bool>>(false);
    CHECK(replayer.Run(10, copy) == 0); // loaded, holding for the files
    copy.Clips->store(true);
    CHECK(replayer.Run(10, copy) == 1);
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

TEST_CASE("An owner's sync point keeps the clip event its game gave it, whatever the graph's last event", "[graph-stream]")
{
    GraphStreams streams;
    streams.Record(0x42, 7);
    const GraphSeed cSeed{1, 1};
    const std::vector<uint32_t> cWords{0, 0};
    streams.OnEvent(0x42, 1, 9, cWords, cSeed); // IdleStop, refused: no files of its own
    streams.OnUpdate(0x42, 1, 0.016f, cWords, cSeed);
    streams.AddPoint(0x42, {.ClipEvent = 3});
    const auto cPoints = streams.TakePoints();
    REQUIRE(cPoints.size() == 1);
    CHECK(cPoints[0].ClipEvent == 3);
    CHECK(cPoints[0].Id == 7);
}
