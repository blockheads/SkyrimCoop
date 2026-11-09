#pragma once

#include <Messages/Payloads.h>
#include <Structs/GraphOps.h>
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
    [[nodiscard]] virtual bool LoadState(const std::string& acState) = 0; // SaveState's (63602)
    virtual void SendEvent(uint16_t aEventId) = 0;                        // 63591, as the copy's replay
    virtual void Update(float aStep) = 0;                                 // 63587, forced
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
    std::optional<uint16_t> TimeDelta;

    // Its owner's run; a run that does not follow the last one is a gap: it waits for a new sync point.
    void Feed(const GraphOpsRun& acRun, const std::vector<GraphOpFrame>& acFrames);
    void Load(const Payloads::GraphSyncPoint& acPoint);
    // Started on a sync point: its graph's inputs are the stream's alone.
    [[nodiscard]] bool Started() const noexcept
    {
        return m_started || m_point.has_value();
    }
    [[nodiscard]] bool NeedsPoint() const noexcept
    {
        return !m_started && !m_point;
    }
    // The ops due by aPlaybackTick, in order (on the graph's thread): the updates it ran.
    size_t Run(uint64_t aPlaybackTick, IGraphTarget& aTarget);

  private:
    struct Queued
    {
        GraphOp Op;
        uint64_t Tick = 0;
        float Step = 0.f;
        uint32_t Next = 0; // an update's index, or the index of the update after an event
    };
    std::deque<Queued> m_queue;
    std::optional<Payloads::GraphSyncPoint> m_point; // to load at the next run
    std::vector<uint32_t> m_words;                   // its graph's after its last op: restored before the next
    uint32_t m_nextFed = 0;
    bool m_fed = false;
    bool m_started = false;
};

// Every actor's stream on one game: the owner's recorders (written on the graphs' threads), the copies' replayers.
struct GraphStreams
{
    // Owner: CharacterService records the NPCs this game owns (players: phase 4).
    void Record(uint32_t aFormId, uint32_t aActorId);
    void Forget(uint32_t aFormId); // no longer owned, or no longer a copy
    [[nodiscard]] bool Recording(uint32_t aFormId) const;
    void OnEvent(uint32_t aFormId, uint64_t aTick, uint16_t aEventId, std::span<const uint32_t> aWords,
                 const GraphSeed& acSeed);
    void OnUpdate(uint32_t aFormId, uint64_t aTick, float aStep, std::span<const uint32_t> aWords,
                  const GraphSeed& acSeed);
    // After the update: true when a copy asked for a sync point, which the caller saves and hands to AddPoint.
    [[nodiscard]] bool OnUpdated(uint32_t aFormId, std::span<const uint32_t> aWords);
    void AddPoint(uint32_t aFormId, Payloads::GraphSyncPoint aPoint); // its Id and Update filled in
    void WantPoint(uint32_t aFormId);
    [[nodiscard]] std::optional<GraphOps> TakeOps();
    [[nodiscard]] std::vector<Payloads::GraphSyncPoint> TakePoints();

    // Copy.
    void Feed(uint32_t aFormId, const GraphOpsRun& acRun, const std::vector<GraphOpFrame>& acFrames);
    void Load(uint32_t aFormId, const Payloads::GraphSyncPoint& acPoint);
    [[nodiscard]] bool OnStream(uint32_t aFormId) const;
    // True once while it waits for a sync point: ask its owner for one.
    [[nodiscard]] bool ShouldAsk(uint32_t aFormId);
    void SetPlayback(uint32_t aFormId, uint64_t aTick);
    size_t Replay(uint32_t aFormId, IGraphTarget& aTarget);

  private:
    struct Owned
    {
        std::mutex Lock;
        uint32_t ActorId = 0;
        GraphRecorder Recorder;
        bool WantsPoint = false;
    };
    struct Copied
    {
        std::mutex Lock;
        GraphReplayer Replayer;
        uint64_t Playback = 0;
        bool Asked = false;
    };
    [[nodiscard]] std::shared_ptr<Owned> FindOwned(uint32_t aFormId) const;
    [[nodiscard]] std::shared_ptr<Copied> FindCopied(uint32_t aFormId, bool aCreate = false);

    mutable std::mutex m_lock;
    std::unordered_map<uint32_t, std::shared_ptr<Owned>> m_owned;
    std::unordered_map<uint32_t, std::shared_ptr<Copied>> m_copied;
    std::vector<Payloads::GraphSyncPoint> m_points;
};
