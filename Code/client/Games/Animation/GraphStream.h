#pragma once

#include <Messages/Payloads.h>
#include <Structs/GraphOps.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// The graph input stream (docs/design/graph-input-stream.md): an owner's graph inputs recorded as ops (variables set
// from outside the graph, events, steps, seed) and replayed, in order, into its copies' graphs from a sync point.

// What the replayer drives: Skyrim's graph (Games/Animation) or the fake's.
struct IGraphTarget
{
    virtual ~IGraphTarget() = default;
    [[nodiscard]] virtual std::vector<uint32_t> Words() = 0;
    virtual void WriteWords(std::span<const uint32_t> aWords) = 0;
    virtual void SetSeed(const GraphSeed& acSeed) = 0;
    [[nodiscard]] virtual bool LoadPoint(const Payloads::GraphSyncPoint& acPoint) = 0; // its state (63602), seed, counters
    virtual void SendEvent(uint16_t aEventId) = 0;                        // 63591, as the copy's replay
    // The event's clip files loading into a process's set, as its owner's process did (ActionClips): null when it
    // needs none or has them. A clip started without its file lasts 0.0133 s (59263).
    [[nodiscard]] virtual std::shared_ptr<std::atomic<bool>> RequestClips(uint8_t aSet, uint16_t aEventId) = 0;
    // The picks its owner's nodes made from outside the graph in the update that follows: its own nodes take them.
    virtual void Picks(std::span<const GraphPick> aPicks) = 0;
    virtual void Update(float aStep) = 0;                                 // 63587, every op at once (interpolation off)
    virtual void Generate() = 0; // the generate pass (63568), where its owner ran one: never on its own
};

// An op with its frame, before batching.
struct GraphRecordedOp
{
    GraphOp Op;
    uint64_t Tick = 0; // the owner's frame tick
    float Step = 0.f;
};

// Owner: one actor's graph inputs, as its graph takes them.
struct GraphRecorder
{
    std::optional<uint16_t> TimeDelta; // its index: the update's step, bit for bit, so the copy writes it

    // Before the graph handles an event / runs an update (its channels already applied, 63590).
    void Event(uint64_t aTick, uint16_t aEventId, std::span<const uint32_t> aWords, const GraphSeed& acSeed);
    void Update(uint64_t aTick, float aStep, std::span<const uint32_t> aWords, const GraphSeed& acSeed);
    // Its process loaded an event's clip files into a set (coop_LoadEventClips 43621).
    void Clips(uint64_t aTick, uint8_t aSet, uint16_t aEventId);
    // Its generate pass (63568), once after each frame its graph updated in: modifiers move there.
    void Generate(uint64_t aTick);
    // During its update, each update the node picks in: its pick from outside the graph (GraphPick). It goes with
    // that update when it changed or the node was idle before. ForgetPicks: a copy started now may lack them.
    void Pick(uint16_t aNode, int32_t aValue);
    void ForgetPicks() noexcept { m_picked.clear(); }
    // After its update: what the graph wrote itself, which the copy's graph writes too.
    void Updated(std::span<const uint32_t> aWords);
    [[nodiscard]] uint32_t Updates() const noexcept
    {
        return m_updates;
    }
    // The ops since the last take; aFirstUpdate: the index of the first update among them.
    [[nodiscard]] std::vector<GraphRecordedOp> Take(uint32_t& aFirstUpdate);

  private:
    void Record(GraphRecordedOp aOp, std::span<const uint32_t> aWords, const GraphSeed& acSeed);

    std::vector<uint32_t> m_after; // its variables as its last op left them: what its copies hold
    std::optional<GraphSeed> m_seed;
    std::vector<GraphRecordedOp> m_ops;
    uint32_t m_updates = 0;
    uint32_t m_takenUpdates = 0;
    struct Picked
    {
        int32_t Value = 0;
        uint32_t Update = 0; // m_updates at its last pick; 0: none
    };
    std::unordered_map<uint16_t, Picked> m_picked; // each node's last pick
    bool m_open = false; // its last op is an update still running: not taken until it ends (its picks go with it)
};

// One batch from several actors' ops: each update's frame in a table they share.
struct GraphRecordedRun
{
    uint32_t ActorId = 0;
    uint32_t FirstUpdate = 0;
    std::vector<GraphRecordedOp> Ops;
};
[[nodiscard]] GraphOps BatchGraphOps(const std::vector<GraphRecordedRun>& acRuns);

// Copy: its owner's ops, replayed on its playback clock from a sync point.
struct GraphReplayer
{
    static constexpr size_t cMaxUpdatesPerRun = 4; // behind: catch up a few updates a frame
    static constexpr uint32_t cMaxClipWaits = 120;  // runs a clip load holds the stream (a cancelled load never ends)
    std::optional<uint16_t> TimeDelta;

    // Its owner's run; a run that does not follow the last one is a gap: it waits for a new sync point.
    void Feed(const GraphOpsRun& acRun, const std::vector<GraphOpFrame>& acFrames);
    void Load(const Payloads::GraphSyncPoint& acPoint);
    // Its graph was replaced (a 3D reload): what follows waits for a new sync point.
    void Restart() noexcept;
    // Started on a sync point: its graph's inputs are the stream's alone.
    [[nodiscard]] bool Started() const noexcept
    {
        return m_started || m_point.has_value();
    }
    [[nodiscard]] bool NeedsPoint() const noexcept
    {
        return !m_started && !m_point;
    }
    // The project of the point it is about to load (0: none waiting, or unknown).
    [[nodiscard]] uint32_t PointProject() const noexcept
    {
        return m_point ? m_point->Project : 0;
    }
    // The ops due by aPlaybackTick, in order (on the graph's thread): the updates it ran.
    size_t Run(uint64_t aPlaybackTick, IGraphTarget& aTarget);
    // The owner's update index it runs now or ran last (a readout: the harness's live check).
    [[nodiscard]] uint32_t CurrentUpdate() const noexcept { return m_current.load(std::memory_order_relaxed); }

  private:
    struct Queued
    {
        GraphOp Op;
        uint64_t Tick = 0;
        float Step = 0.f;
        uint32_t Next = 0; // an update's index, or the index of the update after an event or a generate pass
    };
    std::deque<Queued> m_queue;
    std::optional<Payloads::GraphSyncPoint> m_point; // to load at the next run
    std::vector<uint32_t> m_words;                   // its graph's after its last op: restored before the next
    uint32_t m_nextFed = 0;
    uint64_t m_lastUpdateTick = 0; // the last update fed: a generate pass after it is due with it
    bool m_fed = false;
    bool m_started = false;
    std::atomic<uint32_t> m_current{UINT32_MAX};
    std::vector<std::shared_ptr<std::atomic<bool>>> m_loads; // clip files loading: its point's sets, or the front load op
    bool m_clipsAsked = false;
    uint32_t m_clipWaits = 0;
};

// Every actor's stream on one game: the owner's recorders (written on the graphs' threads), the copies' replayers.
struct GraphStreams
{
    // Owner: CharacterService records the actors this game owns, its player included.
    void Record(uint32_t aFormId, uint32_t aActorId);
    void Forget(uint32_t aFormId); // no longer owned, or no longer a copy
    [[nodiscard]] bool Recording(uint32_t aFormId) const;
    // Recorded and not on its own body: its copies' graphs take its stream alone, so its movement carries no graph
    // variables and it sends no graph points.
    [[nodiscard]] bool Streaming(uint32_t aFormId) const;
    void OnEvent(uint32_t aFormId, uint64_t aTick, uint16_t aEventId, std::span<const uint32_t> aWords,
                 const GraphSeed& acSeed);
    void OnClips(uint32_t aFormId, uint64_t aTick, uint8_t aSet, uint16_t aEventId);
    void OnGenerate(uint32_t aFormId, uint64_t aTick);
    // apGraph: which graph it is; another one (a 3D reload) starts its recording over, so its copies ask for a point.
    void OnUpdate(uint32_t aFormId, uint64_t aTick, float aStep, std::span<const uint32_t> aWords,
                  const GraphSeed& acSeed, const void* apGraph = nullptr);
    void OnPick(uint32_t aFormId, uint16_t aNode, int32_t aValue);
    void OnUpdated(uint32_t aFormId, std::span<const uint32_t> aWords);
    // An owner on its own body (in a pair's instance or clip, 32803): recorded on (two copies of a pair elsewhere each
    // replay their own stream, its clip's wait at the mark a pick), but it gives no sync point while aOwnBody (one
    // restored inside a synchronized clip starts the clip over) and its actions carry what a copy held off its
    // stream needs (Streaming). Returns aOwnBody.
    bool HoldPointsWhile(uint32_t aFormId, bool aOwnBody);
    // An owner in a mount's pair (getting on or off, riding: its body is its mount's): nothing of it is recorded
    // while aOwnBody, and its recording starts over after, so its copies ask for a point. Returns aOwnBody.
    bool PauseWhile(uint32_t aFormId, bool aOwnBody);
    // As an update begins, before OnUpdate (its graph after the generate pass since its last update, which moves
    // blenders' sync and modifiers' outputs): true when a copy asked for a sync point, which the caller saves then.
    [[nodiscard]] bool PointWanted(uint32_t aFormId, const void* apGraph = nullptr);
    // Its Id and Update filled in; its ClipSets are the caller's: what its process holds. With no state (a graph
    // being rebuilt) none is made, and it is wanted again.
    void AddPoint(uint32_t aFormId, Payloads::GraphSyncPoint aPoint);
    void WantPoint(uint32_t aFormId);
    [[nodiscard]] std::optional<GraphOps> TakeOps();
    [[nodiscard]] std::vector<Payloads::GraphSyncPoint> TakePoints();

    // Readouts (the harness's live check): the update an owner's graph or a copy's replay is on, and the actor's id.
    [[nodiscard]] std::optional<uint32_t> UpdateIndex(uint32_t aFormId) const;
    [[nodiscard]] uint32_t ActorIdOf(uint32_t aFormId) const;

    // TimeDelta's index in its graph (the same behaviour files on every game), once its graph is first seen.
    [[nodiscard]] bool NeedsTimeDelta(uint32_t aFormId) const;
    void SetTimeDelta(uint32_t aFormId, std::optional<uint16_t> aIndex);

    // The frame's tick, set by the main thread, for the graphs' threads.
    void SetTick(uint64_t aTick) noexcept { m_tick.store(aTick, std::memory_order_relaxed); }
    [[nodiscard]] uint64_t Tick() const noexcept { return m_tick.load(std::memory_order_relaxed); }

    // Copy.
    void Feed(uint32_t aFormId, const GraphOpsRun& acRun, const std::vector<GraphOpFrame>& acFrames);
    void Load(uint32_t aFormId, const Payloads::GraphSyncPoint& acPoint);
    [[nodiscard]] bool OnStream(uint32_t aFormId) const;
    // A copy on its own body: in a pair on this game (a kill move: BGSSynchronizedAnimationManager's instance), or
    // knocked down (its ragdoll, then its owner's get-up action). Held off its stream while aOwnBody, then it asks for
    // a new sync point. Returns whether it is held.
    bool HoldWhile(uint32_t aFormId, bool aOwnBody);
    [[nodiscard]] bool Held(uint32_t aFormId) const;
    // Its owner's pair began (its half's action, at aTick): off its stream, on its owner's actions, until a sync
    // point from after it. It asks for that point once it has played the action (PairPlayed) and its own body is
    // out of it (HoldWhile's aOwnBody false: its kill move over, on or off its mount).
    void HoldUntilPoint(uint32_t aFormId, uint64_t aTick);
    void PairPlayed(uint32_t aFormId);
    // True once while it waits for a sync point: ask its owner for one.
    [[nodiscard]] bool ShouldAsk(uint32_t aFormId);
    void SetPlayback(uint32_t aFormId, uint64_t aTick);
    // Its graph now, each update on or off its stream: a new one since another project's point was dropped asks again.
    void SeeGraph(uint32_t aFormId, const void* apGraph);
    // apGraph: which graph it replays into; another than the one its point loaded into waits for a new point.
    // aProject: that graph's behaviour project; a point of another project's is dropped, and it asks again only with
    // a new graph (its leveled base becoming its owner's reloads its 3D: SeeGraph).
    size_t Replay(uint32_t aFormId, IGraphTarget& aTarget, const void* apGraph = nullptr, uint32_t aProject = 0);

  private:
    struct Owned
    {
        std::mutex Lock;
        uint32_t ActorId = 0;
        GraphRecorder Recorder;
        const void* Graph = nullptr;
        bool WantsPoint = false;
        bool TimeDeltaKnown = false;
        bool Paused = false; // on its own body (HoldPointsWhile): no sync point, not Streaming
        bool NotRecorded = false; // in a mount's pair (PauseWhile)
    };
    struct Copied
    {
        std::mutex Lock;
        GraphReplayer Replayer;
        const void* Graph = nullptr; // the graph its sync point loaded into
        std::atomic<uint32_t> ActorId{0};
        uint64_t Playback = 0;
        bool TimeDeltaKnown = false;
        bool Asked = false;
        std::atomic<bool> Held{false}; // in a pair on this game: off its stream
        const void* WrongGraph = nullptr; // a graph of another project than its owner's: no point fits it
        uint64_t PointAfter = 0; // held for its owner's pair: until a point newer than this tick
        bool PairPending = false; // that pair's action not played here yet
        bool OwnBody = false;     // its body still in a pair, a kill move, getting on or off a mount
    };
    [[nodiscard]] std::shared_ptr<Owned> FindOwned(uint32_t aFormId) const;
    [[nodiscard]] std::shared_ptr<Copied> FindCopied(uint32_t aFormId, bool aCreate = false);

    mutable std::mutex m_lock;
    std::unordered_map<uint32_t, std::shared_ptr<Owned>> m_owned;
    std::unordered_map<uint32_t, std::shared_ptr<Copied>> m_copied;
    std::vector<Payloads::GraphSyncPoint> m_points;
    std::atomic<uint64_t> m_tick{0};
};
