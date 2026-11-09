#include <Actor.h>
#include <Games/Animation/BSAnimationUpdateData.h>
#include <Games/Animation/GraphRandom.h>
#include <Games/Animation/GraphStream.h>
#include <Games/Animation/NodeMemory.h>
#include <Games/Skyrim/ActionClips.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/BShkbHkxDB.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbNode.h>
#include <Havok/hkbVariableValueSet.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Misc/BSFixedString.h>
#include <World.h>
#include <Wire/Serialization.h>
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

// A class's reflection for NodeMemory, built once (its own members; hkReferencedObject's are not memory).
const NodeMemory::Class* Reflected(const hkClass* apClass, int aDepth = 0)
{
    static std::recursive_mutex s_lock;
    static std::unordered_map<const hkClass*, std::unique_ptr<NodeMemory::Class>> s_classes;
    if (!apClass || aDepth > 4)
        return nullptr;
    std::scoped_lock _{s_lock};
    if (const auto it = s_classes.find(apClass); it != s_classes.end())
        return it->second.get();
    auto pClass = std::make_unique<NodeMemory::Class>();
    pClass->Size = uint32_t(std::max(apClass->objectSize, 0));
    for (int32_t i = 0; i < apClass->numDeclaredMembers; ++i)
    {
        const hkClassMember& cMember = apClass->declaredMembers[i];
        const bool cStruct = cMember.type == NodeMemory::kStruct || (cMember.type == NodeMemory::kArray && cMember.subtype == NodeMemory::kStruct);
        pClass->Members.push_back({cMember.offset, cMember.type, cMember.subtype, uint16_t(std::max<int16_t>(cMember.cArraySize, 0)),
                                   cStruct ? Reflected(cMember.klass, aDepth + 1) : nullptr});
    }
    return s_classes.emplace(apClass, std::move(pClass)).first->second.get();
}

// A node class's internal state class (Havok's naming: <node class>InternalState), from every class the game links
// (hkBuiltinTypeRegistry::StaticLinkedClasses 239939): a state object's own getClassType (vtable 1) returns 0.
const hkClass* StateClass(std::string_view aNodeClass)
{
    static std::mutex s_lock;
    static std::unordered_map<std::string, const hkClass*> s_byName;
    std::scoped_lock _{s_lock};
    if (s_byName.empty())
    {
        POINTER_SKYRIMSE(const hkClass*, s_linked, 239939);
        for (const hkClass* const* ppClass = s_linked.Get(); ppClass && *ppClass; ++ppClass)
            if ((*ppClass)->name)
                s_byName.emplace((*ppClass)->name, *ppClass);
    }
    const auto it = s_byName.find(std::string(aNodeClass) + "InternalState");
    return it == s_byName.end() ? nullptr : it->second;
}

// An hkStringPtr's text: its lowest bit flags a string it owns.
const char* Text(const char* apStringPtr)
{
    return reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(apStringPtr) & ~uintptr_t(1));
}

// Its deleting destructor (vtable 0): Havok's own allocator frees it.
void Release(hkReferencedObject* apObject)
{
    using TDelete = void*(hkReferencedObject*, uint32_t);
    (*reinterpret_cast<TDelete***>(apObject))[0](apObject, 1);
}

// Each active node, in the graph and the sub-behaviours in it (a clip's time, a modifier's last values), whose
// internal state holds no pointer: a state machine's points at nodes of its own graph (loaded with the graph state).
template <class T> void EachActiveNode(const hkbBehaviorGraphNode* apGraph, T&& aWork, int aDepth = 0)
{
    if (!apGraph || !apGraph->activeNodes || aDepth > 8)
        return;
    const auto& cNodes = *apGraph->activeNodes;
    for (int32_t i = 0; i < cNodes.size; ++i)
    {
        hkbNode* pNode = cNodes.data[i].nodeClone;
        const hkClass* pClass = pNode ? pNode->GetClassType() : nullptr;
        if (!pClass || !pClass->name)
            continue;
        const std::string_view cClass = pClass->name;
        if (cClass == "hkbBehaviorReferenceGenerator")
            EachActiveNode(static_cast<const hkbBehaviorReferenceGenerator*>(pNode)->behavior, aWork, aDepth + 1);
        else if (const hkClass* pState = StateClass(cClass))
            if (const auto* pReflected = Reflected(pState); pReflected && !NodeMemory::HasPointers(*pReflected))
                aWork(*pNode, std::format("{}/{}", apGraph->name ? Text(apGraph->name) : "", pNode->id), cClass);
    }
}

// The owner's active modifiers' internal states: count, then each node's key, its state's class and its bytes.
std::string TakeMemory(const BShkbAnimationGraph& acGraph)
{
    std::vector<std::tuple<std::string, std::string, std::string>> entries;
    uint32_t modifiers = 0, stateless = 0, unreflected = 0;
    EachActiveNode(reinterpret_cast<const hkbBehaviorGraphNode*>(acGraph.behaviorGraph), [&](hkbNode& aNode, std::string aKey, std::string_view aClass) {
        ++modifiers;
        const hkClass* pClass = StateClass(aClass);
        hkReferencedObject* pState = pClass ? aNode.CreateInternalState() : nullptr;
        if (!pState)
        {
            ++stateless;
            return;
        }
        aNode.GetInternalState(*pState);
        if (const auto* pReflected = Reflected(pClass); pReflected && pClass->name)
        {
            Wire::Buffer bytes;
            Wire::Buffer::Writer writer(&bytes);
            NodeMemory::Write(writer, reinterpret_cast<const uint8_t*>(pState), *pReflected);
            entries.emplace_back(std::move(aKey), pClass->name, std::string(reinterpret_cast<const char*>(bytes.GetData()), writer.Size()));
        }
        else
            ++unreflected;
        Release(pState);
    });
    spdlog::debug("Graph stream: a sync point with {} of {} active nodes' memory ({} without, {} unreflected)", entries.size(),
                  modifiers, stateless, unreflected);
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    Wire::Serialization::WriteVarInt(writer, entries.size());
    for (const auto& [cKey, cClass, cBytes] : entries)
    {
        Wire::Serialization::WriteString(writer, cKey);
        Wire::Serialization::WriteString(writer, cClass);
        Wire::Serialization::WriteString(writer, cBytes);
    }
    return std::string(reinterpret_cast<const char*>(buffer.GetData()), writer.Size());
}

// Onto the copy's active modifiers of the same key and class, after its graph state loaded (whose activation reset
// them).
void GiveMemory(BShkbAnimationGraph& aGraph, const std::string& acMemory)
{
    Wire::Buffer::Reader reader{std::span(reinterpret_cast<const uint8_t*>(acMemory.data()), acMemory.size())};
    uint64_t count = 0;
    if (acMemory.empty() || !Wire::Serialization::TryReadVarInt(reader, count) || count > 4096)
        return;
    std::unordered_map<std::string, std::pair<std::string, std::string>> byKey;
    for (uint64_t i = 0; i < count; ++i)
    {
        std::string key = Wire::Serialization::ReadString(reader);
        std::string klass = Wire::Serialization::ReadString(reader);
        byKey[std::move(key)] = {std::move(klass), Wire::Serialization::ReadString(reader)};
    }
    uint32_t applied = 0, unmatched = 0, noKey = 0, otherClass = 0, notFitting = 0;
    std::string missing;
    EachActiveNode(reinterpret_cast<const hkbBehaviorGraphNode*>(aGraph.behaviorGraph), [&](hkbNode& aNode, std::string aKey, std::string_view aClass) {
        const auto it = byKey.find(aKey);
        if (it == byKey.end())
        {
            ++unmatched;
            ++noKey;
            if (missing.size() < 200)
                missing += aKey + " ";
            return;
        }
        const hkClass* pClass = StateClass(aClass);
        hkReferencedObject* pState = pClass ? aNode.CreateInternalState() : nullptr;
        if (!pState)
            return;
        aNode.GetInternalState(*pState); // its arrays sized by its own node
        const auto* pReflected = Reflected(pClass);
        Wire::Buffer::Reader bytes{std::span(reinterpret_cast<const uint8_t*>(it->second.second.data()), it->second.second.size())};
        if (!pReflected || !pClass->name || it->second.first != pClass->name)
        {
            ++unmatched;
            ++otherClass;
        }
        else if (!NodeMemory::Read(bytes, reinterpret_cast<uint8_t*>(pState), *pReflected))
        {
            ++unmatched;
            ++notFitting;
        }
        else
        {
            aNode.SetInternalState(*pState);
            ++applied;
        }
        Release(pState);
    });
    spdlog::debug("Graph stream: {} of {} nodes given their owner's memory (sent {}; no key {}, other class {}, not fitting {}; "
                  "missing {})", applied, applied + unmatched, byKey.size(), noKey, otherClass, notFitting, missing);
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
        if (!Graph.holder || !Graph.holder->LoadGraphPoint(acPoint.Seed.Seed, acPoint.State))
            return false;
        SetSeed(acPoint.Seed);
        Graph.holder->SetGraphCounters(GraphRandom::Unpack(acPoint.Counters));
        GiveMemory(Graph, acPoint.Memory);
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
        streams.Replay(cFormId, target);
        return target.Result;
    }
    if (!streams.Recording(cFormId))
        return RealUpdate(apGraph, apData, apScratch);
    if (streams.NeedsTimeDelta(cFormId))
        streams.SetTimeDelta(cFormId, TimeDeltaIndex(*apGraph));
    // Forced: its step is the frame's, never a far graph's chunk (its first updates come before CharacterService's).
    apData->force = true;
    streams.OnUpdate(cFormId, streams.Tick(), apData->deltaTime, Words(*apGraph), Seed(*apGraph));
    const char cResult = RealUpdate(apGraph, apData, apScratch);
    if (streams.OnUpdated(cFormId, Words(*apGraph)))
    {
        Payloads::GraphSyncPoint point;
        point.Tick = streams.Tick();
        uint32_t seed = 0;
        std::vector<GraphRandom::Counter> counters;
        apGraph->holder->SaveGraphPoint(seed, point.State, counters);
        point.Words = Words(*apGraph);
        point.Seed = Seed(*apGraph);
        point.Counters = GraphRandom::Pack(counters);
        point.Memory = TakeMemory(*apGraph);
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

Hooks::Initializer s_graphStreamHooks([]() {
    HOOK_ID(63587, &RealUpdate, HookUpdate);
    HOOK_ID(63591, &RealSendEvent, HookSendEvent);
});
} // namespace
