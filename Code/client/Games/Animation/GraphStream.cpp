#include "GraphStream.h"

#include <algorithm>
#include <bit>
#include <map>

void GraphRecorder::Record(GraphRecordedOp aOp, std::span<const uint32_t> aWords, const GraphSeed& acSeed)
{
    const uint32_t cStepBits = std::bit_cast<uint32_t>(aOp.Step);
    for (size_t i = 0; i < aWords.size() && i <= UINT16_MAX; ++i)
    {
        // An update's TimeDelta: the copy writes the step there, so another value goes (a far graph's chunk).
        if (!aOp.Op.Event && TimeDelta && i == *TimeDelta)
        {
            if (aWords[i] != cStepBits)
                aOp.Op.Changed.emplace_back(static_cast<uint16_t>(i), aWords[i]);
            continue;
        }
        if (i < m_after.size() && m_after[i] == aWords[i])
            continue;
        aOp.Op.Changed.emplace_back(static_cast<uint16_t>(i), aWords[i]);
    }
    if (!m_seed || *m_seed != acSeed)
        aOp.Op.Seed = m_seed = acSeed;
    // An event's own writes go with the next op; an update's are replaced after it runs (Updated).
    m_after.assign(aWords.begin(), aWords.end());
    m_ops.push_back(std::move(aOp));
}

void GraphRecorder::Event(uint64_t aTick, uint16_t aEventId, std::span<const uint32_t> aWords, const GraphSeed& acSeed)
{
    Record({{.Event = true, .EventId = aEventId}, aTick, 0.f}, aWords, acSeed);
}

void GraphRecorder::Clips(uint64_t aTick, uint8_t aSet, uint16_t aEventId)
{
    m_ops.push_back({{.Event = true, .EventId = aEventId, .ClipSet = aSet}, aTick, 0.f});
}

void GraphRecorder::Generate(uint64_t aTick)
{
    m_ops.push_back({{.Generate = true}, aTick, 0.f});
}

void GraphRecorder::Update(uint64_t aTick, float aStep, std::span<const uint32_t> aWords, const GraphSeed& acSeed)
{
    Record({{}, aTick, aStep}, aWords, acSeed);
    ++m_updates;
}

void GraphRecorder::Updated(std::span<const uint32_t> aWords)
{
    m_after.assign(aWords.begin(), aWords.end());
}

std::vector<GraphRecordedOp> GraphRecorder::Take(uint32_t& aFirstUpdate)
{
    aFirstUpdate = m_takenUpdates;
    m_takenUpdates = m_updates;
    return std::exchange(m_ops, {});
}

GraphOps BatchGraphOps(const std::vector<GraphRecordedRun>& acRuns)
{
    // Every update's (tick, step) once, in order: the actors' updates in one frame share it.
    std::map<std::pair<uint64_t, uint32_t>, uint32_t> frames;
    for (const auto& cRun : acRuns)
        for (const auto& cOp : cRun.Ops)
            if (!cOp.Op.Event && !cOp.Op.Generate)
                frames.emplace(std::pair{cOp.Tick, std::bit_cast<uint32_t>(cOp.Step)}, 0);
    GraphOps ops;
    for (auto& [cKey, index] : frames)
    {
        index = static_cast<uint32_t>(ops.Frames.size());
        ops.Frames.push_back({cKey.first, std::bit_cast<float>(cKey.second)});
    }
    for (const auto& cRun : acRuns)
    {
        GraphOpsRun& run = ops.Runs.emplace_back(GraphOpsRun{cRun.ActorId, cRun.FirstUpdate, {}});
        for (const auto& cOp : cRun.Ops)
        {
            GraphOp op = cOp.Op;
            if (!op.Event && !op.Generate)
                op.Frame = frames.at({cOp.Tick, std::bit_cast<uint32_t>(cOp.Step)});
            run.Ops.push_back(std::move(op));
        }
    }
    return ops;
}

void GraphReplayer::Feed(const GraphOpsRun& acRun, const std::vector<GraphOpFrame>& acFrames)
{
    if (m_fed && acRun.FirstUpdate != m_nextFed)
    {
        // A gap: what follows has no start without a new sync point.
        m_queue.clear();
        m_started = false;
        m_point.reset();
    }
    m_fed = true;
    uint32_t next = acRun.FirstUpdate;
    for (const auto& cOp : acRun.Ops)
    {
        // An event is due with the update after it (its owner had it in that update's frame): until that update
        // comes, maybe in the next run, it waits.
        Queued queued{cOp, UINT64_MAX, 0.f, next};
        if (cOp.Generate)
            queued.Tick = m_lastUpdateTick; // its owner ran it right after that update, in the same frame
        else if (!cOp.Event && cOp.Frame < acFrames.size())
        {
            queued.Tick = m_lastUpdateTick = acFrames[cOp.Frame].Tick;
            queued.Step = acFrames[cOp.Frame].Step;
            for (auto it = m_queue.rbegin(); it != m_queue.rend() && (it->Op.Event || it->Op.Generate); ++it)
                if (it->Op.Event && it->Tick == UINT64_MAX)
                    it->Tick = queued.Tick;
            ++next;
        }
        m_queue.push_back(std::move(queued));
    }
    m_nextFed = next;
    // Waiting for its first sync point, it keeps the latest only.
    constexpr size_t cMaxWaiting = 4096;
    while (!m_started && m_queue.size() > cMaxWaiting)
        m_queue.pop_front();
}

void GraphReplayer::Load(const Payloads::GraphSyncPoint& acPoint)
{
    m_point = acPoint;
    m_loads.clear();
    m_clipsAsked = false;
    m_clipWaits = 0;
}

void GraphReplayer::Restart() noexcept
{
    m_started = false;
    m_point.reset();
    m_loads.clear();
    m_clipsAsked = false;
    m_clipWaits = 0;
}

size_t GraphReplayer::Run(uint64_t aPlaybackTick, IGraphTarget& aTarget)
{
    // Clip files loading hold the stream, a bounded number of runs (a load the process cancels never ends).
    bool waited = false;
    const auto cHeld = [&] {
        std::erase_if(m_loads, [](const auto& acLoading) { return acLoading->load(std::memory_order_acquire); });
        if (m_loads.empty())
            return false;
        if (!std::exchange(waited, true))
            ++m_clipWaits;
        return m_clipWaits <= cMaxClipWaits;
    };
    if (m_point)
    {
        // The clip sets its owner's process held first: a clip binds its file as it activates (the point's).
        if (!std::exchange(m_clipsAsked, true))
            for (const auto& [cSet, cEvent] : m_point->ClipSets)
                if (auto loading = aTarget.RequestClips(cSet, cEvent))
                    m_loads.push_back(std::move(loading));
        if (cHeld())
            return 0;
        m_loads.clear();
        m_clipWaits = 0;
        m_clipsAsked = false;
        const auto cPoint = std::move(*m_point);
        m_point.reset();
        if (!aTarget.LoadPoint(cPoint))
        {
            m_started = false;
            return 0;
        }
        aTarget.WriteWords(cPoint.Words);
        m_words = aTarget.Words();
        // Its point is its owner's graph as update cPoint.Update began: what came before, the events and loads just
        // before that update too, is in it.
        while (!m_queue.empty() && (m_queue.front().Next < cPoint.Update ||
                                    ((m_queue.front().Op.Event || m_queue.front().Op.Generate) && m_queue.front().Next == cPoint.Update)))
            m_queue.pop_front();
        m_started = true;
    }
    if (!m_started)
        return 0;
    size_t updates = 0;
    // The cap never parts an update from its generate pass: its pose would be an older one's for a frame.
    while (!m_queue.empty() && (updates < cMaxUpdatesPerRun || m_queue.front().Op.Generate) && m_queue.front().Tick <= aPlaybackTick)
    {
        const GraphOp& cFront = m_queue.front().Op;
        if (cFront.Event && cFront.ClipSet && !std::exchange(m_clipsAsked, true))
            if (auto loading = aTarget.RequestClips(cFront.ClipSet, cFront.EventId)) // its owner's load, in its place
                m_loads.push_back(std::move(loading));
        if (cHeld())
            break;
        m_loads.clear();
        m_clipWaits = 0;
        m_clipsAsked = false;
        const Queued cOp = std::move(m_queue.front());
        m_queue.pop_front();
        if (cOp.Op.ClipSet)
            continue;
        if (cOp.Op.Generate)
        {
            aTarget.Generate();
            m_words = aTarget.Words();
            continue;
        }
        // Its variables are the stream's alone: its own writes since (channels, 63590) are put back.
        std::vector<uint32_t> words = m_words;
        // TimeDelta holds the step unless its owner's held another value, which then came with the op.
        if (!cOp.Op.Event && TimeDelta && *TimeDelta < words.size())
            words[*TimeDelta] = std::bit_cast<uint32_t>(cOp.Step);
        for (const auto& [cIndex, cValue] : cOp.Op.Changed)
        {
            if (cIndex >= words.size())
                words.resize(cIndex + 1);
            words[cIndex] = cValue;
        }
        aTarget.WriteWords(words);
        if (cOp.Op.Seed)
            aTarget.SetSeed(*cOp.Op.Seed);
        if (cOp.Op.Event)
            aTarget.SendEvent(cOp.Op.EventId);
        else
        {
            m_current.store(cOp.Next, std::memory_order_relaxed);
            aTarget.Update(cOp.Step);
            ++updates;
        }
        m_words = aTarget.Words();
    }
    return updates;
}

std::shared_ptr<GraphStreams::Owned> GraphStreams::FindOwned(uint32_t aFormId) const
{
    std::scoped_lock _{m_lock};
    const auto it = m_owned.find(aFormId);
    return it == m_owned.end() ? nullptr : it->second;
}

std::shared_ptr<GraphStreams::Copied> GraphStreams::FindCopied(uint32_t aFormId, bool aCreate)
{
    std::scoped_lock _{m_lock};
    auto it = m_copied.find(aFormId);
    if (it == m_copied.end())
    {
        if (!aCreate)
            return nullptr;
        it = m_copied.emplace(aFormId, std::make_shared<Copied>()).first;
    }
    return it->second;
}

void GraphStreams::Record(uint32_t aFormId, uint32_t aActorId)
{
    std::scoped_lock _{m_lock};
    auto& owned = m_owned[aFormId];
    if (!owned)
        owned = std::make_shared<Owned>();
    owned->ActorId = aActorId;
}

void GraphStreams::Forget(uint32_t aFormId)
{
    std::scoped_lock _{m_lock};
    m_owned.erase(aFormId);
    m_copied.erase(aFormId);
}

bool GraphStreams::Recording(uint32_t aFormId) const
{
    return FindOwned(aFormId) != nullptr;
}

void GraphStreams::OnClips(uint32_t aFormId, uint64_t aTick, uint8_t aSet, uint16_t aEventId)
{
    if (const auto pOwned = FindOwned(aFormId))
    {
        std::scoped_lock _{pOwned->Lock};
        pOwned->Recorder.Clips(aTick, aSet, aEventId);
    }
}

void GraphStreams::OnGenerate(uint32_t aFormId, uint64_t aTick)
{
    if (const auto pOwned = FindOwned(aFormId))
    {
        std::scoped_lock _{pOwned->Lock};
        pOwned->Recorder.Generate(aTick);
    }
}

void GraphStreams::OnEvent(uint32_t aFormId, uint64_t aTick, uint16_t aEventId, std::span<const uint32_t> aWords,
                           const GraphSeed& acSeed)
{
    if (const auto pOwned = FindOwned(aFormId))
    {
        std::scoped_lock _{pOwned->Lock};
        pOwned->Recorder.Event(aTick, aEventId, aWords, acSeed);
    }
}

void GraphStreams::OnUpdate(uint32_t aFormId, uint64_t aTick, float aStep, std::span<const uint32_t> aWords,
                            const GraphSeed& acSeed, const void* apGraph)
{
    if (const auto pOwned = FindOwned(aFormId))
    {
        std::scoped_lock _{pOwned->Lock};
        if (pOwned->Graph && apGraph && pOwned->Graph != apGraph)
        {
            pOwned->Recorder = GraphRecorder{}; // a new graph: its copies see the gap and ask for a point
            pOwned->TimeDeltaKnown = false;
        }
        if (apGraph)
            pOwned->Graph = apGraph;
        pOwned->Recorder.Update(aTick, aStep, aWords, acSeed);
    }
}

void GraphStreams::OnUpdated(uint32_t aFormId, std::span<const uint32_t> aWords)
{
    if (const auto pOwned = FindOwned(aFormId))
    {
        std::scoped_lock _{pOwned->Lock};
        pOwned->Recorder.Updated(aWords);
    }
}

bool GraphStreams::PointWanted(uint32_t aFormId, const void* apGraph)
{
    const auto pOwned = FindOwned(aFormId);
    if (!pOwned)
        return false;
    std::scoped_lock _{pOwned->Lock};
    if (apGraph && pOwned->Graph && pOwned->Graph != apGraph)
        return false; // a new graph: its recording starts over at this update, and its copies ask again
    return std::exchange(pOwned->WantsPoint, false);
}

void GraphStreams::AddPoint(uint32_t aFormId, Payloads::GraphSyncPoint aPoint)
{
    const auto pOwned = FindOwned(aFormId);
    if (!pOwned)
        return;
    {
        std::scoped_lock _{pOwned->Lock};
        aPoint.Id = pOwned->ActorId;
        aPoint.Update = pOwned->Recorder.Updates();
    }
    std::scoped_lock _{m_lock};
    m_points.push_back(std::move(aPoint));
}

void GraphStreams::WantPoint(uint32_t aFormId)
{
    if (const auto pOwned = FindOwned(aFormId))
    {
        std::scoped_lock _{pOwned->Lock};
        pOwned->WantsPoint = true;
    }
}

std::optional<GraphOps> GraphStreams::TakeOps()
{
    std::vector<std::shared_ptr<Owned>> owned;
    {
        std::scoped_lock _{m_lock};
        for (const auto& [cFormId, pOwned] : m_owned)
            owned.push_back(pOwned);
    }
    std::vector<GraphRecordedRun> runs;
    for (const auto& pOwned : owned)
    {
        std::scoped_lock _{pOwned->Lock};
        GraphRecordedRun run{pOwned->ActorId, 0, {}};
        run.Ops = pOwned->Recorder.Take(run.FirstUpdate);
        if (!run.Ops.empty())
            runs.push_back(std::move(run));
    }
    if (runs.empty())
        return std::nullopt;
    return BatchGraphOps(runs);
}

std::vector<Payloads::GraphSyncPoint> GraphStreams::TakePoints()
{
    std::scoped_lock _{m_lock};
    return std::exchange(m_points, {});
}

void GraphStreams::Feed(uint32_t aFormId, const GraphOpsRun& acRun, const std::vector<GraphOpFrame>& acFrames)
{
    const auto pCopied = FindCopied(aFormId, true);
    pCopied->ActorId.store(acRun.ActorId, std::memory_order_relaxed);
    std::scoped_lock _{pCopied->Lock};
    const bool cWasStarted = pCopied->Replayer.Started();
    pCopied->Replayer.Feed(acRun, acFrames);
    if (cWasStarted && pCopied->Replayer.NeedsPoint())
        pCopied->Asked = false; // a gap: ask again
}

void GraphStreams::Load(uint32_t aFormId, const Payloads::GraphSyncPoint& acPoint)
{
    const auto pCopied = FindCopied(aFormId, true);
    std::scoped_lock _{pCopied->Lock};
    if (!pCopied->Held) // in a pair: it asks again once out of it
        pCopied->Replayer.Load(acPoint);
}

bool GraphStreams::HoldWhile(uint32_t aFormId, bool aOwnBody)
{
    const auto pCopied = FindCopied(aFormId);
    if (!pCopied)
        return false;
    std::scoped_lock _{pCopied->Lock};
    if (aOwnBody && !pCopied->Held)
    {
        pCopied->Held = true;
        pCopied->Replayer.Restart();
        pCopied->Graph = nullptr;
    }
    else if (!aOwnBody && pCopied->Held)
    {
        pCopied->Held = false;
        pCopied->Asked = false;
    }
    return pCopied->Held;
}

bool GraphStreams::Held(uint32_t aFormId) const
{
    std::scoped_lock _{m_lock};
    const auto it = m_copied.find(aFormId);
    return it != m_copied.end() && it->second->Held;
}

bool GraphStreams::OnStream(uint32_t aFormId) const
{
    std::shared_ptr<Copied> pCopied;
    {
        std::scoped_lock _{m_lock};
        const auto it = m_copied.find(aFormId);
        if (it == m_copied.end())
            return false;
        pCopied = it->second;
    }
    std::scoped_lock _{pCopied->Lock};
    return pCopied->Replayer.Started();
}

bool GraphStreams::ShouldAsk(uint32_t aFormId)
{
    const auto pCopied = FindCopied(aFormId);
    if (!pCopied)
        return false;
    std::scoped_lock _{pCopied->Lock};
    if (!pCopied->Replayer.NeedsPoint() || pCopied->Asked || pCopied->Held || pCopied->WrongGraph)
        return false;
    pCopied->Asked = true;
    return true;
}

void GraphStreams::SetPlayback(uint32_t aFormId, uint64_t aTick)
{
    if (const auto pCopied = FindCopied(aFormId))
    {
        std::scoped_lock _{pCopied->Lock};
        pCopied->Playback = aTick;
    }
}

void GraphStreams::SeeGraph(uint32_t aFormId, const void* apGraph)
{
    const auto pCopied = FindCopied(aFormId);
    if (!pCopied)
        return;
    std::scoped_lock _{pCopied->Lock};
    if (pCopied->WrongGraph && pCopied->WrongGraph != apGraph)
    {
        pCopied->WrongGraph = nullptr;
        pCopied->Asked = false;
    }
}

size_t GraphStreams::Replay(uint32_t aFormId, IGraphTarget& aTarget, const void* apGraph, uint32_t aProject)
{
    const auto pCopied = FindCopied(aFormId);
    if (!pCopied)
        return 0;
    std::scoped_lock _{pCopied->Lock};
    if (pCopied->Graph && apGraph && pCopied->Graph != apGraph)
    {
        // Its graph was replaced since its point loaded: ask for a new one.
        pCopied->Replayer.Restart();
        pCopied->Graph = nullptr;
        pCopied->Asked = false;
        return 0;
    }
    if (const uint32_t cPoint = pCopied->Replayer.PointProject(); cPoint && aProject && cPoint != aProject)
    {
        pCopied->Replayer.Restart(); // its owner's point is another project's
        pCopied->WrongGraph = apGraph;
        return 0;
    }
    const bool cLoading = pCopied->Replayer.Started();
    const size_t cUpdates = pCopied->Replayer.Run(pCopied->Playback, aTarget);
    if (cLoading && pCopied->Replayer.NeedsPoint())
        pCopied->Asked = false; // its point did not load: ask again
    else if (pCopied->Replayer.Started() && !pCopied->Graph)
        pCopied->Graph = apGraph;
    return cUpdates;
}

bool GraphStreams::NeedsTimeDelta(uint32_t aFormId) const
{
    std::shared_ptr<Owned> pOwned;
    std::shared_ptr<Copied> pCopied;
    {
        std::scoped_lock _{m_lock};
        if (const auto it = m_owned.find(aFormId); it != m_owned.end())
            pOwned = it->second;
        if (const auto it = m_copied.find(aFormId); it != m_copied.end())
            pCopied = it->second;
    }
    if (pOwned)
    {
        std::scoped_lock _{pOwned->Lock};
        if (!pOwned->TimeDeltaKnown)
            return true;
    }
    if (pCopied)
    {
        std::scoped_lock _{pCopied->Lock};
        if (!pCopied->TimeDeltaKnown)
            return true;
    }
    return false;
}

void GraphStreams::SetTimeDelta(uint32_t aFormId, std::optional<uint16_t> aIndex)
{
    if (const auto pOwned = FindOwned(aFormId))
    {
        std::scoped_lock _{pOwned->Lock};
        pOwned->Recorder.TimeDelta = aIndex;
        pOwned->TimeDeltaKnown = true;
    }
    if (const auto pCopied = FindCopied(aFormId))
    {
        std::scoped_lock _{pCopied->Lock};
        pCopied->Replayer.TimeDelta = aIndex;
        pCopied->TimeDeltaKnown = true;
    }
}

std::optional<uint32_t> GraphStreams::UpdateIndex(uint32_t aFormId) const
{
    std::shared_ptr<Owned> pOwned;
    std::shared_ptr<Copied> pCopied;
    {
        std::scoped_lock _{m_lock};
        if (const auto it = m_owned.find(aFormId); it != m_owned.end())
            pOwned = it->second;
        if (const auto it = m_copied.find(aFormId); it != m_copied.end())
            pCopied = it->second;
    }
    if (pOwned)
    {
        std::scoped_lock _{pOwned->Lock};
        const uint32_t cUpdates = pOwned->Recorder.Updates();
        return cUpdates ? std::optional(cUpdates - 1) : std::nullopt;
    }
    if (pCopied) // its lock is held while it replays: the index is atomic
    {
        const uint32_t cCurrent = pCopied->Replayer.CurrentUpdate();
        return cCurrent != UINT32_MAX ? std::optional(cCurrent) : std::nullopt;
    }
    return std::nullopt;
}

uint32_t GraphStreams::ActorIdOf(uint32_t aFormId) const
{
    std::scoped_lock _{m_lock};
    if (const auto it = m_owned.find(aFormId); it != m_owned.end())
        return it->second->ActorId; // set by Record, on the main thread before its graph is recorded
    if (const auto it = m_copied.find(aFormId); it != m_copied.end())
        return it->second->ActorId.load(std::memory_order_relaxed);
    return 0;
}
