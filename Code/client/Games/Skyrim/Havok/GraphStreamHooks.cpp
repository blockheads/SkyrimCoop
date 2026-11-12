#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/Animation/BSAnimationUpdateData.h>
#include <Games/Animation/GraphStream.h>
#include <Games/Animation/ReplayState.h>
#include <Games/Skyrim/ActionClips.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/BShkbHkxDB.h>
#include <Havok/BSSynchronizedClipGenerator.h>
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

// The graph input stream's hooks (docs/design/graph-input-stream.md): an owned actor's graph inputs recorded; a copy on
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

// Its behaviour project ("WolfProject"), hashed: never 0.
uint32_t Project(const BShkbAnimationGraph& acGraph)
{
    uint32_t hash = 2166136261u;
    for (const char* p = acGraph.projectName; p && *p; ++p)
        hash = (hash ^ uint8_t(*p)) * 16777619u;
    return hash ? hash : 1;
}

// The graph of an actor its copies have: any NPC's; of this game's player the third-person one (its manager's first).
// Its first-person graph, updated and sent events beside it under the same holder, is no copy's.
// Only an actor's: furniture and weapons hold graphs too (a forge's), and are no Actor to read.
bool CopiesHave(const BShkbAnimationGraph& acGraph)
{
    return acGraph.holder->formType == Actor::Type && (acGraph.holder->formID != 0x14 || acGraph.holder->GraphKey(0) == &acGraph);
}

// An owner on its own body, off its stream: in a pair's instance (32808), in a kill move, or in a pair's clip
// (bIsSynced: an attacker's or a rider's runs on after the instance ends). A sync point taken in a synchronized clip
// starts it over on the copy.
bool OwnBody(Actor& aHolder)
{
    return aHolder.InPair() || aHolder.IsInKillMove() || aHolder.InPairClip();
}

// An owner's own-body spell, by its first event: a kill move's half (ReplayState::KillMoveHalf) is recorded through
// while that kill move lasts; any other (a mount's) is not.
enum class PairSpell : uint8_t
{
    kOther,    // not a kill move's, or its kill move is over (its clip's flag can outlast it, into a mount's pair)
    kStarting, // a kill move's half taken; its flag comes with the action's effects (39004), a moment later
    kKillMove, // in its kill move
};
std::mutex s_pairLock;
std::unordered_map<uint32_t, PairSpell> s_pairSpells;

// An owner's own body for this update or event: through a kill move's pair it is recorded on and only gives no sync
// point; through any other (a mount's) it is not recorded (true), as before. apEvent: the event its graph takes now.
bool NotRecorded(GraphStreams& aStreams, uint32_t aFormId, Actor& aHolder, const BSFixedString* apEvent = nullptr)
{
    const bool cOwnBody = OwnBody(aHolder);
    bool killMove = false;
    {
        std::scoped_lock _{s_pairLock};
        if (!cOwnBody)
            s_pairSpells.erase(aFormId);
        else
        {
            const auto [it, cNew] = s_pairSpells.try_emplace(aFormId, PairSpell::kOther);
            const bool cHalf = apEvent && apEvent->data && ReplayState::KillMoveHalf(apEvent->data);
            if (cHalf && (cNew || it->second == PairSpell::kOther))
                it->second = PairSpell::kStarting;
            const bool cInKillMove = aHolder.IsInKillMove();
            if (it->second == PairSpell::kStarting && cInKillMove)
                it->second = PairSpell::kKillMove;
            else if (it->second == PairSpell::kKillMove && !cInKillMove)
                it->second = PairSpell::kOther;
            killMove = it->second != PairSpell::kOther;
        }
    }
    const bool cPaused = aStreams.PauseWhile(aFormId, cOwnBody && !killMove);
    aStreams.HoldPointsWhile(aFormId, cOwnBody);
    return cPaused;
}

using TUpdate = char(BShkbAnimationGraph*, BSAnimationUpdateData*, void*);
TUpdate* RealUpdate = nullptr;
using TBehaviourUpdate = void(hkbBehaviorGraph*, hkEventContext*, float);
TBehaviourUpdate* RealBehaviourUpdate = nullptr;
using TGenerate = uint64_t(BShkbAnimationGraph*, char);
TGenerate* RealGenerate = nullptr;
// An owner's 63587 under way, for its behaviour graph's update under it.
thread_local BShkbAnimationGraph* s_owner = nullptr;
using TSendEvent = bool(BShkbAnimationGraph*, const BSFixedString*);
TSendEvent* RealSendEvent = nullptr;
using TMatchRagdollPose = void(hkbPoseMatchingGenerator*, void*, float);
TMatchRagdollPose* RealMatchRagdollPose = nullptr;
// The owner whose behaviour graph's update runs on this thread (0: none): its nodes' picks go with that update.
thread_local uint32_t s_recording = 0;
// A copy's replayed update on this thread: its owner's picks in it.
thread_local std::span<const GraphPick> s_picks;

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
        // Its owner's walk to come: the restore's own walk cleared it (60079).
        Graph.behaviorGraph->stateOrTransitionChanged = acPoint.ActiveNodesChanged;
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
    std::vector<GraphPick> OwnersPicks;
    void Picks(std::span<const GraphPick> aPicks) override { OwnersPicks.assign(aPicks.begin(), aPicks.end()); }
    void Update(float aStep) override
    {
        BSAnimationUpdateData data = Data;
        data.deltaTime = aStep;
        // Every op's step at once, at the engine's own LOD and foot IK: `force` also turns foot IK on for an actor
        // the engine would not solve it for, which held a dropped creature in the air.
        data.force = false;
        data.interpAllowed = false;
        s_picks = OwnersPicks;
        Result = RealUpdate(&Graph, &data, Scratch);
        s_picks = {};
        OwnersPicks.clear();
    }
    // Inline, not through the behaviour job queue (63568's second argument): the same pass.
    void Generate() override { RealGenerate(&Graph, 0); }
};

// coop_BShkbAnimationGraph_UpdateInterpolated (63587): one graph's update, its channels already applied (63590).
char HookUpdate(BShkbAnimationGraph* apGraph, BSAnimationUpdateData* apData, void* apScratch)
{
    if (s_inStream || !apGraph || !apGraph->holder || !apGraph->behaviorGraph || !apData || !CopiesHave(*apGraph))
        return RealUpdate(apGraph, apData, apScratch);
    auto& streams = World::Get().GetGraphStreams();
    const uint32_t cFormId = apGraph->holder->formID;
    // On its own body (in a pair): off its owner's stream, and back from a new sync point after. Its own half of a
    // pair plays out after the instance: its kill move's end, getting on or off its mount.
    if (streams.Held(cFormId))
        streams.HoldWhile(cFormId, ReplayState::CopyOwnBody(apGraph->holder->InPair(), apGraph->holder->IsInKillMove(),
                                                            (apGraph->holder->flags2 & Actor::GETTING_ON_OFF_MOUNT) != 0,
                                                            apGraph->holder->actorState.GetSitSleepState()));
    streams.SeeGraph(cFormId, apGraph);
    if (streams.OnStream(cFormId))
    {
        if (streams.NeedsTimeDelta(cFormId))
            streams.SetTimeDelta(cFormId, TimeDeltaIndex(*apGraph));
        // Its owner's ops due by now instead of its own update; none due, its graph holds its pose.
        InStream _;
        SkyrimGraphTarget target(*apGraph, *apData, apScratch);
        streams.Replay(cFormId, target, apGraph, Project(*apGraph));
        // Its generate passes are its owner's, run in the replay: 63589 only interpolates its pose (63581).
        apGraph->behaviourUpdated = false;
        return target.Result;
    }
    // In a pair it is recorded on (its clip's wait at the mark is a pick, 63050), but gives no sync point; in a
    // mount's it is not recorded.
    if (!streams.Recording(cFormId) || NotRecorded(streams, cFormId, *apGraph->holder))
        return RealUpdate(apGraph, apData, apScratch);
    if (streams.NeedsTimeDelta(cFormId))
        streams.SetTimeDelta(cFormId, TimeDeltaIndex(*apGraph));
    // No interpolation: it steps every frame, as its copies replay it, at the engine's own LOD (not `force`, whose
    // full LOD off camera made creatures hover). Its behaviour graph's update records each real step.
    apData->interpAllowed = false;
    s_owner = apGraph;
    const char cResult = RealUpdate(apGraph, apData, apScratch);
    s_owner = nullptr;
    return cResult;
}

// hkbBehaviorGraph's update (vfunc 5, 58329) under an owner's 63587: its real step, when it steps.
void HookBehaviourUpdate(hkbBehaviorGraph* apBehaviour, hkEventContext* apContext, float aStep)
{
    BShkbAnimationGraph* pGraph = s_owner;
    if (s_inStream || !pGraph || pGraph->behaviorGraph != apBehaviour)
        return RealBehaviourUpdate(apBehaviour, apContext, aStep); // a sub-behaviour's, or no owner's
    s_owner = nullptr;
    auto& streams = World::Get().GetGraphStreams();
    const uint32_t cFormId = pGraph->holder->formID;
    SendClipLoads(streams, cFormId, *pGraph);
    // A sync point as this update begins: its graph after its generate pass, as this update will take it.
    if (streams.PointWanted(cFormId, pGraph))
    {
        Payloads::GraphSyncPoint point;
        point.Tick = streams.Tick();
        point.State = GraphInternalState::Take(*pGraph);
        point.Words = Words(*pGraph);
        point.Seed = Seed(*pGraph);
        point.ActiveNodesChanged = pGraph->behaviorGraph->stateOrTransitionChanged != 0;
        point.Project = Project(*pGraph);
        point.ClipSets = HeldClipSets(*pGraph);
        streams.AddPoint(cFormId, std::move(point));
    }
    streams.OnUpdate(cFormId, streams.Tick(), aStep, Words(*pGraph), Seed(*pGraph), pGraph);
    s_recording = cFormId;
    RealBehaviourUpdate(apBehaviour, apContext, aStep);
    s_recording = 0;
    streams.OnUpdated(cFormId, Words(*pGraph));
}

// hkbPoseMatchingGenerator's match step (59583), in its update: the get-up candidate its ragdoll lies closest to, the
// node's one read from outside its graph. An owner's pick goes with its update; a copy's node takes it and reads nothing.
void HookMatchRagdollPose(hkbPoseMatchingGenerator* apNode, void* apContext, float aStep)
{
    if (s_inStream)
    {
        for (const auto& cPick : s_picks)
            if (cPick.Node == apNode->id)
            {
                spdlog::debug("Graph stream: a copy's node {} '{}' takes its owner's match {} (had {})", apNode->id,
                              apNode->name ? apNode->name : "", cPick.Value, apNode->currentMatch);
                apNode->TakeMatch(cPick.Value);
            }
        const int8_t cMode = std::exchange(apNode->mode, hkbPoseMatchingGenerator::kPlaying); // no match of its own
        RealMatchRagdollPose(apNode, apContext, aStep);
        apNode->mode = cMode;
        return;
    }
    const int32_t cBefore = apNode->currentMatch;
    RealMatchRagdollPose(apNode, apContext, aStep);
    // Its match while it matches, each update: its recorder sends it when it changed or a copy may lack it.
    const auto* pContext = static_cast<const hkEventContext*>(apContext);
    if (apNode->mode != hkbPoseMatchingGenerator::kMatching || apNode->currentMatch < 0 || !pContext || !pContext->character)
        return;
    const auto* pGraph = reinterpret_cast<const BShkbAnimationGraph*>(reinterpret_cast<const uint8_t*>(pContext->character) -
                                                                      offsetof(BShkbAnimationGraph, character));
    if (!pGraph->holder || !CopiesHave(*pGraph))
        return;
    if (apNode->currentMatch != cBefore)
        spdlog::debug("Graph stream: {:X}'s node {} '{}' matched {} (had {}); in its recorded update: {}", pGraph->holder->formID, apNode->id,
                      apNode->name ? apNode->name : "", apNode->currentMatch, cBefore, s_recording == pGraph->holder->formID);
    World::Get().GetGraphStreams().OnPick(pGraph->holder->formID, apNode->id, apNode->currentMatch);
}

using TUpdateSynchronizedClip = void(BSSynchronizedClipGenerator*, const hkbContext&, float);
TUpdateSynchronizedClip* RealUpdateSynchronizedClip = nullptr;

// Its owner's scene answers, standing in for a copy's default scene through one replayed update.
struct OwnersScene final : BSSynchronizedClipGenerator::Scene
{
    uint64_t SizeAndReferences = 0x1FFFF; // hkReferencedObject's, as the default scene's: never freed
    bool InScene = true;
    bool AtMarks = true;
    bool AllInScene() override { return InScene; }
    bool AllAtMarks() override { return AtMarks; }
};

enum ScenePick : int32_t
{
    kAllAtMarks = 1 << 0,
    kAllInScene = 1 << 1,
    kAtMark = 1 << 2,
};

// BSSynchronizedClipGenerator::Update (63050, vtable 5): a pair's clip, which advances only while its scene answers
// that every actor is at its mark, its one read outside its graph. An owner's answers go with its update; a copy
// replaying its stream takes them where its own scene (the default one, no instance here) says yes at once.
void HookUpdateSynchronizedClip(BSSynchronizedClipGenerator* apThis, const hkbContext& acContext, float aTimestep)
{
    const auto* pGraph = acContext.character
                             ? reinterpret_cast<const BShkbAnimationGraph*>(reinterpret_cast<const uint8_t*>(acContext.character) -
                                                                            offsetof(BShkbAnimationGraph, character))
                             : nullptr;
    if (s_inStream)
    {
        // Its owner's answers from this update's pick, else those of its last update (sent when they change).
        int32_t answers = (apThis->allAtMarks ? kAllAtMarks : 0) | (apThis->allInScene ? kAllInScene : 0) | (apThis->atMark ? kAtMark : 0);
        for (const auto& cPick : s_picks)
            if (cPick.Node == apThis->id)
                answers = cPick.Value;
        OwnersScene scene;
        scene.AtMarks = (answers & kAllAtMarks) != 0;
        scene.InScene = (answers & kAllInScene) != 0;
        auto* pOwn = std::exchange(apThis->scene, &scene);
        RealUpdateSynchronizedClip(apThis, acContext, aTimestep);
        apThis->scene = pOwn;
        apThis->atMark = (answers & kAtMark) != 0;
        return;
    }
    RealUpdateSynchronizedClip(apThis, acContext, aTimestep);
    if (!pGraph || !pGraph->holder)
        return;
    // A copy's half in an instance on this game is at its mark: its owner's game placed it, and the engine cannot
    // walk a copy there (docs/re/systems/actor-ai-and-copies.md).
    if (pGraph->holder->formType == Actor::Type && pGraph->holder->GetExtension()->IsCopy())
    {
        apThis->atMark = true;
        return;
    }
    if (CopiesHave(*pGraph))
        World::Get().GetGraphStreams().OnPick(pGraph->holder->formID, apThis->id,
                                              (apThis->allAtMarks ? kAllAtMarks : 0) | (apThis->allInScene ? kAllInScene : 0) |
                                                  (apThis->atMark ? kAtMark : 0));
}

// BShkbAnimationGraph::SendEvent (63591): every input event of a graph, before it handles it.
bool HookSendEvent(BShkbAnimationGraph* apGraph, const BSFixedString* apEvent)
{
    if (s_inStream || !apGraph || !apGraph->holder || !apEvent || !CopiesHave(*apGraph))
        return RealSendEvent(apGraph, apEvent);
    auto& streams = World::Get().GetGraphStreams();
    const uint32_t cFormId = apGraph->holder->formID;
    // A copy on the stream takes its owner's events alone, but in a pair on this game (a kill move's half, 32803):
    // that one plays on its own body, off the stream.
    if (streams.OnStream(cFormId) && !streams.HoldWhile(cFormId, apGraph->holder->InPair()))
        return false;
    // An owner's half of a pair comes through here too (both already in the instance: 32803).
    if (streams.Recording(cFormId) && !NotRecorded(streams, cFormId, *apGraph->holder, apEvent))
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

// coop_BShkbAnimationGraph_Generate (63568), from 63589 when the graph updated this frame: an owner's, for its copies.
uint64_t HookGenerate(BShkbAnimationGraph* apGraph, char aQueued)
{
    if (!s_inStream && apGraph && apGraph->holder && CopiesHave(*apGraph))
    {
        auto& streams = World::Get().GetGraphStreams();
        if (streams.Recording(apGraph->holder->formID))
            streams.OnGenerate(apGraph->holder->formID, streams.Tick());
    }
    return RealGenerate(apGraph, aQueued);
}

Hooks::Initializer s_graphStreamHooks([]() {
    HOOK_ID(63587, &RealUpdate, HookUpdate);
    HOOK_ID(58329, &RealBehaviourUpdate, HookBehaviourUpdate);
    HOOK_ID(63568, &RealGenerate, HookGenerate);
    HOOK_ID(63591, &RealSendEvent, HookSendEvent);
    HOOK_ID(59583, &RealMatchRagdollPose, HookMatchRagdollPose);
    HOOK_ID(63050, &RealUpdateSynchronizedClip, HookUpdateSynchronizedClip);
    HOOK_ID(43621, &RealLoadEventClips, HookLoadEventClips);
    HOOK_ID(43622, &RealLoadEventClipsQueued, HookLoadEventClipsQueued);
    HOOK_ID(43624, &RealLoadManagerClips, HookLoadManagerClips);
});
} // namespace
