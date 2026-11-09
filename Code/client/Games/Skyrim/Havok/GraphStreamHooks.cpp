#include <Actor.h>
#include <Games/Animation/BSAnimationUpdateData.h>
#include <Games/Animation/GraphStream.h>
#include <Games/Skyrim/ActionClips.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/BShkbHkxDB.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/GraphInternalState.h>
#include <Havok/hkbNode.h>
#include <Havok/hkbVariableValueSet.h>
#include <Havok/hkEventContext.h>
#include <Havok/bhkWorldM.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Misc/BSFixedString.h>
#include <World.h>
#include <map>
#include <mutex>
#include <unordered_map>

// The graph input stream's hooks (docs/design/graph-input-stream.md): an owned NPC's graph inputs recorded; a copy on
// the stream driven by its owner's ops instead of its own update and events.
namespace
{
// The stream's own calls into the engine (the replayer's, LoadState's zero-step update 63602): passed through.
thread_local bool s_inStream = false;

struct InStream
{
    InStream() noexcept { s_inStream = true; }
    ~InStream() noexcept { s_inStream = false; }
};

std::vector<uint32_t> Words(const BShkbAnimationGraph& acGraph)
{
    const auto* pSet = acGraph.behaviorGraph ? acGraph.behaviorGraph->animationVariables : nullptr;
    return pSet && pSet->data ? std::vector<uint32_t>(pSet->data, pSet->data + pSet->size) : std::vector<uint32_t>{};
}

GraphSeed Seed(const BShkbAnimationGraph& acGraph)
{
    const auto* pRandom = acGraph.behaviorGraph ? acGraph.behaviorGraph->random : nullptr;
    return pRandom ? GraphSeed{pRandom->seed, pRandom->current} : GraphSeed{};
}

// A name's value in one of the project's tables (hkxDB +0x7C variables, +0xAC events), as 63591 looks an event up:
// the hash of the pooled string (68221), then the bucket's chain.
const hkHashTable::Entry* Find(const hkHashTable& acTable, const char* acpName)
{
    if (!acTable.buckets || !acTable.bucketCount || !acpName)
        return nullptr;
    using THash = uint32_t(uint32_t&, const char*);
    POINTER_SKYRIMSE(THash, s_hash, 68221);
    uint32_t hash = 0;
    s_hash(hash, acpName);
    const auto* pEntry = &acTable.buckets[hash & (acTable.bucketCount - 1)];
    if (!pEntry->next)
        return nullptr;
    while (pEntry->key.data != acpName)
    {
        pEntry = pEntry->next;
        if (pEntry == acTable.end)
            return nullptr;
    }
    return pEntry;
}

std::optional<uint16_t> EventId(const BShkbAnimationGraph& acGraph, const BSFixedString& acEvent)
{
    if (!acGraph.hkxDB)
        return std::nullopt;
    const auto* pEntry = Find(acGraph.hkxDB->hashTable, acEvent.data);
    return pEntry && pEntry->value >= 0 && pEntry->value <= UINT16_MAX ? std::optional(uint16_t(pEntry->value)) : std::nullopt;
}

// The event of that id (both games load the same behaviour files): its pooled name, from the event table.
const BSFixedString* EventName(const BShkbAnimationGraph& acGraph, uint16_t aEventId)
{
    if (!acGraph.hkxDB || !acGraph.hkxDB->hashTable.buckets)
        return nullptr;
    const auto& cTable = acGraph.hkxDB->hashTable;
    for (uint32_t i = 0; i < cTable.bucketCount; ++i)
    {
        const auto* pEntry = &cTable.buckets[i];
        if (!pEntry->next)
            continue;
        for (; pEntry && pEntry != cTable.end; pEntry = pEntry->next)
            if (pEntry->value == aEventId)
                return &pEntry->key;
    }
    return nullptr;
}

std::optional<uint16_t> TimeDeltaIndex(const BShkbAnimationGraph& acGraph)
{
    if (!acGraph.hkxDB)
        return std::nullopt;
    const BSFixedString cName("TimeDelta");
    const auto* pEntry = Find(acGraph.hkxDB->animationVariables, cName.data);
    return pEntry && pEntry->value >= 0 && pEntry->value <= UINT16_MAX ? std::optional(uint16_t(pEntry->value)) : std::nullopt;
}

// Each actor's clip sets (an action's, an instant interaction's, a movement idle's), by its graph manager: a save's
// reload names no actor (43624). The event loaded into each, which a sync point names; its loads since its last op,
// for its stream.
struct ClipLoads
{
    std::map<uint8_t, std::string> Held;
    std::vector<std::pair<uint8_t, std::string>> Unsent;
};
std::mutex s_clipLock;
std::unordered_map<const void*, ClipLoads> s_clipLoads;

bool Mirrored(uint32_t aSet) noexcept
{
    return aSet == ActionClips::kAction || aSet == ActionClips::kInstant || aSet == ActionClips::kMovement;
}

void RecordClipLoad(const void* apManager, uint32_t aSet, const char* acpEvent)
{
    if (!apManager || !acpEvent || !Mirrored(aSet))
        return;
    std::scoped_lock _{s_clipLock};
    if (s_clipLoads.size() > 4096)
        s_clipLoads.clear();
    auto& loads = s_clipLoads[apManager];
    loads.Held[uint8_t(aSet)] = acpEvent;
    if (loads.Unsent.size() < 16)
        loads.Unsent.emplace_back(uint8_t(aSet), acpEvent);
}

// Its loads since the last op into its stream, before the op that follows them (on its owner's game, the action is
// performed once its files are in).
void SendClipLoads(GraphStreams& aStreams, uint32_t aFormId, const BShkbAnimationGraph& acGraph)
{
    std::vector<std::pair<uint8_t, std::string>> unsent;
    {
        std::scoped_lock _{s_clipLock};
        const auto it = s_clipLoads.find(acGraph.holder ? acGraph.holder->GraphManagerKey() : nullptr);
        if (it == s_clipLoads.end() || it->second.Unsent.empty())
            return;
        unsent = std::exchange(it->second.Unsent, {});
    }
    for (const auto& [cSet, cEvent] : unsent)
    {
        BSFixedString event(cEvent.c_str());
        if (const auto cId = EventId(acGraph, event))
            aStreams.OnClips(aFormId, aStreams.Tick(), cSet, *cId);
        event.Release();
    }
}

std::vector<std::pair<uint8_t, uint16_t>> HeldClipSets(const BShkbAnimationGraph& acGraph)
{
    std::map<uint8_t, std::string> held;
    {
        std::scoped_lock _{s_clipLock};
        if (const auto it = s_clipLoads.find(acGraph.holder ? acGraph.holder->GraphManagerKey() : nullptr); it != s_clipLoads.end())
        {
            held = it->second.Held;
            it->second.Unsent.clear(); // its point names them
        }
    }
    std::vector<std::pair<uint8_t, uint16_t>> sets;
    for (const auto& [cSet, cEvent] : held)
    {
        BSFixedString event(cEvent.c_str());
        if (const auto cId = EventId(acGraph, event))
            sets.emplace_back(cSet, *cId);
        event.Release();
    }
    return sets;
}

using TUpdate = char(BShkbAnimationGraph*, BSAnimationUpdateData*, void*);
TUpdate* RealUpdate = nullptr;
using TSendEvent = bool(BShkbAnimationGraph*, const BSFixedString*);
TSendEvent* RealSendEvent = nullptr;

// The replayer's surface over a copy's graph, inside its update (63587).
struct SkyrimGraphTarget final : IGraphTarget
{
    BShkbAnimationGraph& Graph;
    BSAnimationUpdateData Data;
    void* Scratch;
    char Result = 0;
    SkyrimGraphTarget(BShkbAnimationGraph& aGraph, const BSAnimationUpdateData& acData, void* apScratch)
        : Graph(aGraph), Data(acData), Scratch(apScratch)
    {
    }

    std::vector<uint32_t> Words() override { return ::Words(Graph); }
    void WriteWords(std::span<const uint32_t> aWords) override
    {
        auto* pSet = Graph.behaviorGraph ? Graph.behaviorGraph->animationVariables : nullptr;
        if (pSet && pSet->data)
            std::copy_n(aWords.begin(), std::min<size_t>(aWords.size(), pSet->size), pSet->data);
    }
    void SetSeed(const GraphSeed& acSeed) override
    {
        if (Graph.behaviorGraph && Graph.behaviorGraph->random)
            *Graph.behaviorGraph->random = {acSeed.Seed, acSeed.Current};
    }
    bool LoadPoint(const Payloads::GraphSyncPoint& acPoint) override
    {
        if (!Graph.holder || !Graph.behaviorGraph)
            return false;
        // Its owner's whole graph through Havok's own restore (58373): each node's state, and the children those
        // states choose; not Skyrim's save, which leaves generators to choose again (Faendal's turn blend).
        hkEventContext context(&Graph.character, Graph.hkWorldM ? Graph.hkWorldM->GetWorld() : nullptr);
        if (!GraphInternalState::Give(Graph, context, acPoint.State, acPoint.Words))
            return false;
        SetSeed(acPoint.Seed);
        return true;
    }
    void SendEvent(uint16_t aEventId) override
    {
        if (const auto* pName = EventName(Graph, aEventId))
            RealSendEvent(&Graph, pName);
    }
    std::shared_ptr<std::atomic<bool>> RequestClips(uint8_t aSet, uint16_t aEventId) override
    {
        if (aSet != ActionClips::kAction && aSet != ActionClips::kInstant && aSet != ActionClips::kMovement)
            return nullptr;
        const auto* pName = EventName(Graph, aEventId);
        auto loading = pName && Graph.holder ? ActionClips::Request(*Graph.holder, pName->data, ActionClips::Set(aSet)) : nullptr;
        spdlog::debug("Graph stream: copy {:X} loads {}'s clip files into set {}: {}", Graph.holder ? Graph.holder->formID : 0,
                      pName && pName->data ? pName->data : "?", aSet, loading ? "loading" : "none");
        return loading;
    }
    void Update(float aStep) override
    {
        BSAnimationUpdateData data = Data;
        data.deltaTime = aStep;
        data.force = true;
        Result = RealUpdate(&Graph, &data, Scratch);
    }
};

// coop_BShkbAnimationGraph_UpdateInterpolated (63587): one graph's update, its channels already applied (63590).
char HookUpdate(BShkbAnimationGraph* apGraph, BSAnimationUpdateData* apData, void* apScratch)
{
    if (s_inStream || !apGraph || !apGraph->holder || !apGraph->behaviorGraph || !apData)
        return RealUpdate(apGraph, apData, apScratch);
    auto& streams = World::Get().GetGraphStreams();
    const uint32_t cFormId = apGraph->holder->formID;
    if (streams.OnStream(cFormId))
    {
        if (streams.NeedsTimeDelta(cFormId))
            streams.SetTimeDelta(cFormId, TimeDeltaIndex(*apGraph));
        // Its owner's ops due by now instead of its own update; none due, its graph holds its pose.
        InStream _;
        SkyrimGraphTarget target(*apGraph, *apData, apScratch);
        streams.Replay(cFormId, target, apGraph);
        return target.Result;
    }
    if (!streams.Recording(cFormId))
        return RealUpdate(apGraph, apData, apScratch);
    if (streams.NeedsTimeDelta(cFormId))
        streams.SetTimeDelta(cFormId, TimeDeltaIndex(*apGraph));
    // Forced: its step is the frame's, never a far graph's chunk (its first updates come before CharacterService's).
    apData->force = true;
    SendClipLoads(streams, cFormId, *apGraph);
    // A sync point as this update begins: its graph after its generate pass, as this update will take it.
    if (streams.PointWanted(cFormId, apGraph))
    {
        Payloads::GraphSyncPoint point;
        point.Tick = streams.Tick();
        point.State = GraphInternalState::Take(*apGraph);
        point.Words = Words(*apGraph);
        point.Seed = Seed(*apGraph);
        point.ClipSets = HeldClipSets(*apGraph);
        if (!point.State.empty())
            streams.AddPoint(cFormId, std::move(point));
    }
    streams.OnUpdate(cFormId, streams.Tick(), apData->deltaTime, Words(*apGraph), Seed(*apGraph), apGraph);
    const char cResult = RealUpdate(apGraph, apData, apScratch);
    streams.OnUpdated(cFormId, Words(*apGraph));
    return cResult;
}

// BShkbAnimationGraph::SendEvent (63591): every input event of a graph, before it handles it.
bool HookSendEvent(BShkbAnimationGraph* apGraph, const BSFixedString* apEvent)
{
    if (s_inStream || !apGraph || !apGraph->holder || !apEvent)
        return RealSendEvent(apGraph, apEvent);
    auto& streams = World::Get().GetGraphStreams();
    const uint32_t cFormId = apGraph->holder->formID;
    if (streams.OnStream(cFormId))
        return false; // a copy on the stream takes its owner's events alone
    if (streams.Recording(cFormId))
    {
        SendClipLoads(streams, cFormId, *apGraph);
        if (const auto cId = EventId(*apGraph, *apEvent))
            streams.OnEvent(cFormId, streams.Tick(), *cId, Words(*apGraph), Seed(*apGraph));
    }
    return RealSendEvent(apGraph, apEvent);
}

using TLoadEventClips = void*(BSFixedString*, Actor*, uint32_t, uint32_t, void*, void*, bool);
TLoadEventClips* RealLoadEventClips = nullptr;
using TLoadEventClipsQueued = void*(BSFixedString*, Actor*, uint32_t, uint32_t, void*, void*, void*, bool);
TLoadEventClipsQueued* RealLoadEventClipsQueued = nullptr;
using TLoadManagerClips = void*(BSFixedString*, void*, uint32_t, uint32_t, void*, void*, void*, bool);
TLoadManagerClips* RealLoadManagerClips = nullptr;

void LogOwnerLoad(Actor* apActor, const BSFixedString* apEvent, uint32_t aSet)
{
    if (apActor && apEvent && apEvent->data && World::Get().GetGraphStreams().Recording(apActor->formID))
        spdlog::debug("Graph stream: owner {:X} loads {}'s clip files into set {}", apActor->formID, apEvent->data, aSet);
}

// coop_LoadEventClips (43621) and its queued twin (43622): an actor's game loads an event's clip files into one of its
// sets (3 the action's, 4 an instant interaction's, 47 a movement idle's: 39625, 39291, 41694).
void* HookLoadEventClips(BSFixedString* apEvent, Actor* apActor, uint32_t aSet, uint32_t aFlags, void* apCallback, void* apLock, bool aKeep)
{
    LogOwnerLoad(apActor, apEvent, aSet);
    if (apActor && apEvent)
        RecordClipLoad(apActor->GraphManagerKey(), aSet, apEvent->data);
    return RealLoadEventClips(apEvent, apActor, aSet, aFlags, apCallback, apLock, aKeep);
}

void* HookLoadEventClipsQueued(BSFixedString* apEvent, Actor* apActor, uint32_t aSet, uint32_t aFlags, void* apCallback, void* apLock,
                               void* apMore, bool aKeep)
{
    LogOwnerLoad(apActor, apEvent, aSet);
    if (apActor && apEvent)
        RecordClipLoad(apActor->GraphManagerKey(), aSet, apEvent->data);
    return RealLoadEventClipsQueued(apEvent, apActor, aSet, aFlags, apCallback, apLock, apMore, aKeep);
}

// coop_LoadEventClipsForManager (43624): a process's sets reloaded as a save loads (AIProcess::FinishLoadGame).
void* HookLoadManagerClips(BSFixedString* apEvent, void* apManager, uint32_t aSet, uint32_t aFlags, void* apCallback, void* apLock, void* apMore,
                           bool aKeep)
{
    if (apEvent)
    {
        spdlog::debug("Graph stream: a save's reload loads {}'s clip files into set {}", apEvent->data ? apEvent->data : "", aSet);
        RecordClipLoad(apManager, aSet, apEvent->data);
    }
    return RealLoadManagerClips(apEvent, apManager, aSet, aFlags, apCallback, apLock, apMore, aKeep);
}

Hooks::Initializer s_graphStreamHooks([]() {
    HOOK_ID(63587, &RealUpdate, HookUpdate);
    HOOK_ID(63591, &RealSendEvent, HookSendEvent);
    HOOK_ID(43621, &RealLoadEventClips, HookLoadEventClips);
    HOOK_ID(43622, &RealLoadEventClipsQueued, HookLoadEventClipsQueued);
    HOOK_ID(43624, &RealLoadManagerClips, HookLoadManagerClips);
});
} // namespace
