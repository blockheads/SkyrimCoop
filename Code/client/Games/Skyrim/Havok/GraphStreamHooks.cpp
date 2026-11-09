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

// The event whose clip files each owned NPC's process loaded last (coop_LoadEventClips 43621, the action set): its
// copies load them as their stream starts (AIProcess 39625 holds one action set).
std::mutex s_clipLock;
std::unordered_map<uint32_t, std::string> s_clipEvents;

std::string ClipEventOf(uint32_t aFormId)
{
    std::scoped_lock _{s_clipLock};
    const auto it = s_clipEvents.find(aFormId);
    return it == s_clipEvents.end() ? std::string{} : it->second;
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
    std::shared_ptr<std::atomic<bool>> RequestClips(uint16_t aEventId) override
    {
        const auto* pName = EventName(Graph, aEventId);
        return pName && Graph.holder ? ActionClips::Request(*Graph.holder, pName->data) : nullptr;
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
    streams.OnUpdate(cFormId, streams.Tick(), apData->deltaTime, Words(*apGraph), Seed(*apGraph), apGraph);
    const char cResult = RealUpdate(apGraph, apData, apScratch);
    if (streams.OnUpdated(cFormId, Words(*apGraph)))
    {
        Payloads::GraphSyncPoint point;
        point.Tick = streams.Tick();
        point.State = GraphInternalState::Take(*apGraph);
        point.Words = Words(*apGraph);
        point.Seed = Seed(*apGraph);
        if (const auto cClips = ClipEventOf(cFormId); !cClips.empty())
        {
            BSFixedString event(cClips.c_str());
            if (const auto cId = EventId(*apGraph, event))
                point.ClipEvent = *cId;
            event.Release();
        }
        if (!point.State.empty())
            streams.AddPoint(cFormId, std::move(point));
    }
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
        if (const auto cId = EventId(*apGraph, *apEvent))
            streams.OnEvent(cFormId, streams.Tick(), *cId, Words(*apGraph), Seed(*apGraph));
    return RealSendEvent(apGraph, apEvent);
}

using TLoadEventClips = void*(BSFixedString*, Actor*, uint32_t, uint32_t, void*, void*, bool);
TLoadEventClips* RealLoadEventClips = nullptr;

// coop_LoadEventClips (43621): an actor's process loads an event's clip files into one of its sets (3: the action's).
void* HookLoadEventClips(BSFixedString* apEvent, Actor* apActor, uint32_t aSet, uint32_t aFlags, void* apCallback, void* apLock, bool aKeep)
{
    constexpr uint32_t cActionSet = 3;
    if (aSet == cActionSet && apActor && apEvent && apEvent->data && World::Get().GetGraphStreams().Recording(apActor->formID))
    {
        std::scoped_lock _{s_clipLock};
        s_clipEvents[apActor->formID] = apEvent->data;
    }
    return RealLoadEventClips(apEvent, apActor, aSet, aFlags, apCallback, apLock, aKeep);
}

Hooks::Initializer s_graphStreamHooks([]() {
    HOOK_ID(63587, &RealUpdate, HookUpdate);
    HOOK_ID(63591, &RealSendEvent, HookSendEvent);
    HOOK_ID(43621, &RealLoadEventClips, HookLoadEventClips);
});
} // namespace
