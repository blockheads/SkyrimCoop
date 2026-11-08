#include "GraphReplay.h"
#include "EngineCalls.h"
#include <Actor.h>
#include <Games/Animation/BSAnimationUpdateData.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbNode.h>
#include <Havok/hkbVariableValueSet.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Misc/BSFixedString.h>
#include <Games/Animation/GraphRandom.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <optional>

namespace
{
// One input to the owner's graph, in order: an event, or an update with its step. Words: the graph's variables then.
struct Op
{
    bool Event = false;
    std::string Name;
    float Step = 0.f;
    uint32_t Update = 0; // the owner's update this is, or the next one for an event
    hkPseudoRandomGenerator Random{};
    std::vector<uint32_t> Words;
};

std::mutex s_lock;
std::atomic<uint32_t> s_base{0};
BShkbAnimationGraph* s_owner = nullptr;
BShkbAnimationGraph* s_shadow = nullptr;
uint32_t s_ownerId = 0, s_shadowId = 0;
std::deque<Op> s_ops;
size_t s_cursor = 0;
uint32_t s_ownerUpdates = 0;
// A graph after an update: its active machines' states (what must match) and its variables (tallied apart: a node
// reading the world, foot IK or look-at, writes outputs that need not change a state).
struct Snapshot
{
    std::string States;
    std::vector<uint32_t> Words;
};
std::vector<Snapshot> s_ownerDigests;
struct VariableDiff
{
    uint32_t Updates = 0, First = 0, Owner = 0, Shadow = 0;
};
std::map<uint32_t, VariableDiff> s_variableDiffs;
uint32_t s_replayed = 0, s_equal = 0, s_dropped = 0, s_unforced = 0;
double s_ownerSeconds = 0.0; // the owner's recorded steps, added up
// What the ops would weigh on the wire, each against the one before: a 3-byte header (kind, step or event length),
// 6 bytes per changed variable (u16 index, u32 value), the event's name.
uint64_t s_wireBytes = 0, s_changedWords = 0, s_events = 0;
std::vector<uint32_t> s_lastWords;
void CountWire(const Op& acOp)
{
    uint32_t changed = 0;
    for (size_t i = 0; i < acOp.Words.size(); ++i)
        changed += i >= s_lastWords.size() || s_lastWords[i] != acOp.Words[i];
    s_lastWords = acOp.Words;
    s_changedWords += changed;
    s_events += acOp.Event;
    s_wireBytes += 3 + 6 * changed + acOp.Name.size();
}
std::vector<std::string> s_lastOps;
std::optional<nlohmann::json> s_firstMismatch;
thread_local bool s_replaying = false;
thread_local bool s_loading = false; // LoadState's own zero-step update (63602)

// Where the shadow starts: the owner's sync point (graph state, seed, every-N counters) right after one of its updates;
// the replay goes on from the next op. Two fresh graphs differ already: activation draws each actor's own seed.
struct StartPoint
{
    uint32_t Seed = 0;
    std::string State;
    std::vector<GraphRandom::Counter> Counters;
    size_t FirstOp = 0;
};
bool s_wantPoint = false;
std::optional<StartPoint> s_point;
bool s_shadowStarted = false;

enum class Role { kNone, kOwner, kShadow };

std::vector<uint32_t> ReadWords(const BShkbAnimationGraph& acGraph)
{
    const auto* pSet = acGraph.behaviorGraph ? acGraph.behaviorGraph->animationVariables : nullptr;
    return pSet && pSet->data ? std::vector<uint32_t>(pSet->data, pSet->data + pSet->size) : std::vector<uint32_t>{};
}

void WriteWords(BShkbAnimationGraph& aGraph, const std::vector<uint32_t>& acWords)
{
    auto* pSet = aGraph.behaviorGraph ? aGraph.behaviorGraph->animationVariables : nullptr;
    if (!pSet || !pSet->data)
        return;
    std::copy_n(acWords.begin(), std::min<size_t>(acWords.size(), pSet->size), pSet->data);
}

Snapshot Digest(const BShkbAnimationGraph& acGraph)
{
    std::vector<std::string> active;
    for (const auto& cMachine : TestHarness::EngineCalls::GetGraphMachines(acGraph.holder ? acGraph.holder->formID : 0, ""))
        if (cMachine.Active)
            active.push_back(cMachine.Behaviour + "/" + cMachine.Name + "=" + cMachine.State);
    std::ranges::sort(active);
    Snapshot snapshot{{}, ReadWords(acGraph)};
    for (const auto& cLine : active)
        snapshot.States += (snapshot.States.empty() ? "" : "\n") + cLine;
    return snapshot;
}

std::vector<std::string> Lines(const std::string& acDigest)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= acDigest.size())
    {
        const size_t cEnd = std::min(acDigest.find('\n', start), acDigest.size());
        lines.push_back(acDigest.substr(start, cEnd - start));
        start = cEnd + 1;
    }
    return lines;
}

void Compare(uint32_t aUpdate, const Snapshot& acShadow)
{
    std::scoped_lock _{s_lock};
    ++s_replayed;
    const Snapshot& cOwner = s_ownerDigests[aUpdate];
    for (size_t i = 0; i < std::min(cOwner.Words.size(), acShadow.Words.size()); ++i)
        if (cOwner.Words[i] != acShadow.Words[i])
        {
            auto& diff = s_variableDiffs[uint32_t(i)];
            if (!diff.Updates++)
                diff = {1, aUpdate, cOwner.Words[i], acShadow.Words[i]};
        }
    if (cOwner.States == acShadow.States)
    {
        ++s_equal;
        return;
    }
    if (s_firstMismatch)
        return;
    const auto cOwnerLines = Lines(cOwner.States), cShadowLines = Lines(acShadow.States);
    nlohmann::json ownerOnly = nlohmann::json::array(), shadowOnly = nlohmann::json::array();
    for (const auto& cLine : cOwnerLines)
        if (!std::ranges::contains(cShadowLines, cLine))
            ownerOnly.push_back(cLine);
    for (const auto& cLine : cShadowLines)
        if (!std::ranges::contains(cOwnerLines, cLine))
            shadowOnly.push_back(cLine);
    s_firstMismatch = nlohmann::json{{"update", aUpdate}, {"owner_only", ownerOnly}, {"shadow_only", shadowOnly}, {"last_ops", s_lastOps}};
}

Role RoleOf(BShkbAnimationGraph* apGraph, const TESObjectREFR* apRefr)
{
    std::scoped_lock _{s_lock};
    if (apGraph == s_owner)
        return Role::kOwner;
    if (apGraph == s_shadow)
        return Role::kShadow;
    const uint32_t cBase = s_base.load(std::memory_order_relaxed);
    // Only temporary references (placeatme's): the cell's own actors of that base never take a part.
    if (!cBase || !apRefr || apRefr->formType != Actor::Type || (apRefr->formID >> 24) != 0xFF || !apRefr->baseForm ||
        apRefr->baseForm->formID != cBase)
        return Role::kNone;
    if (!s_owner)
    {
        s_owner = apGraph;
        s_ownerId = apRefr->formID;
        return Role::kOwner;
    }
    if (!s_shadow && apRefr->formID != s_ownerId)
    {
        s_shadow = apGraph;
        s_shadowId = apRefr->formID;
        s_wantPoint = true;
        return Role::kShadow;
    }
    return Role::kNone;
}

// coop_BShkbAnimationGraph_UpdateInterpolated (63587): one graph's update; forced, its step is the data's deltaTime.
using TUpdate = char(BShkbAnimationGraph*, BSAnimationUpdateData*, void*);
TUpdate* RealUpdate = nullptr;

char RecordOwnerUpdate(BShkbAnimationGraph* apGraph, BSAnimationUpdateData* apData, void* apScratch)
{
    uint32_t update = 0;
    {
        std::scoped_lock _{s_lock};
        update = s_ownerUpdates;
        Op op{false, {}, apData->deltaTime, update, {}, ReadWords(*apGraph)};
        if (apGraph->behaviorGraph && apGraph->behaviorGraph->random)
            op.Random = *apGraph->behaviorGraph->random;
        s_unforced += !apData->force;
        s_ownerSeconds += apData->deltaTime;
        CountWire(op);
        s_ops.push_back(std::move(op));
    }
    // Forced, its step is the recorded deltaTime (63587); the first updates come before CharacterService forces it.
    apData->force = true;
    const char cResult = RealUpdate(apGraph, apData, apScratch);
    Snapshot digest = Digest(*apGraph);
    std::optional<StartPoint> point;
    if (s_wantPoint && apGraph->holder)
    {
        point.emplace();
        apGraph->holder->SaveGraphPoint(point->Seed, point->State, point->Counters);
    }
    std::scoped_lock _{s_lock};
    s_ownerDigests.push_back(std::move(digest));
    s_ownerUpdates = update + 1;
    if (point && s_wantPoint)
    {
        point->FirstOp = s_ops.size();
        s_point = std::move(point);
        s_wantPoint = false;
    }
    return cResult;
}

// The shadow's engine update: the owner's inputs since the last, in order, instead.
char ReplayIntoShadow(BShkbAnimationGraph* apGraph, BSAnimationUpdateData* apData, void* apScratch)
{
    std::optional<StartPoint> start;
    std::vector<Op> due;
    {
        std::scoped_lock _{s_lock};
        if (!s_point)
            return 0;
        if (!s_shadowStarted)
        {
            start = s_point;
            s_cursor = s_point->FirstOp;
            s_shadowStarted = true;
        }
        for (; s_cursor < s_ops.size(); ++s_cursor)
        {
            const Op& cOp = s_ops[s_cursor];
            if (!cOp.Event && cOp.Update >= s_ownerUpdates)
                break;
            due.push_back(cOp);
        }
    }
    if (start && apGraph->holder)
    {
        s_loading = true;
        apGraph->holder->LoadGraphPoint(start->Seed, start->State);
        apGraph->holder->SetGraphCounters(start->Counters);
        s_loading = false;
    }
    char result = 0;
    for (const Op& cOp : due)
    {
        WriteWords(*apGraph, cOp.Words);
        if (cOp.Event)
        {
            BSFixedString name(cOp.Name.c_str());
            s_replaying = true;
            TestHarness::EngineCalls::SendGraphEvent(apGraph, name);
            s_replaying = false;
        }
        else
        {
            BSAnimationUpdateData data = *apData;
            data.deltaTime = cOp.Step;
            data.force = true;
            result = RealUpdate(apGraph, &data, apScratch);
            Compare(cOp.Update, Digest(*apGraph));
        }
        std::scoped_lock _{s_lock};
        s_lastOps.push_back(cOp.Event ? "event " + cOp.Name : std::format("update {} step {:.4f}", cOp.Update, cOp.Step));
        if (s_lastOps.size() > 8)
            s_lastOps.erase(s_lastOps.begin());
    }
    return result;
}

char HookUpdate(BShkbAnimationGraph* apGraph, BSAnimationUpdateData* apData, void* apScratch)
{
    if (!s_base.load(std::memory_order_relaxed) || !apData || s_loading)
        return RealUpdate(apGraph, apData, apScratch);
    switch (RoleOf(apGraph, apData->refr))
    {
    case Role::kOwner: return RecordOwnerUpdate(apGraph, apData, apScratch);
    case Role::kShadow: return ReplayIntoShadow(apGraph, apData, apScratch);
    default: return RealUpdate(apGraph, apData, apScratch);
    }
}

Hooks::Initializer s_updateHook([]() { HOOK_ID(63587, &RealUpdate, HookUpdate); });
} // namespace

bool GraphReplay::DropEvent(BShkbAnimationGraph* apGraph, const BSFixedString* apEvent) noexcept
{
    if (!s_base.load(std::memory_order_relaxed) || s_replaying)
        return false;
    std::scoped_lock _{s_lock};
    if (apGraph == s_shadow)
    {
        ++s_dropped;
        return true;
    }
    if (apGraph == s_owner)
    {
        s_ops.push_back({true, apEvent && apEvent->data ? apEvent->data : "", 0.f, s_ownerUpdates, {}, ReadWords(*apGraph)});
        CountWire(s_ops.back());
    }
    return false;
}

nlohmann::json GraphReplay::Command(const nlohmann::json& acRequest) noexcept
{
    std::scoped_lock _{s_lock};
    if (acRequest.value("stop", false) || acRequest.contains("arm_like"))
    {
        s_base = 0;
        s_owner = s_shadow = nullptr;
        s_ownerId = s_shadowId = 0;
        s_ops.clear();
        s_cursor = s_ownerUpdates = s_replayed = s_equal = s_dropped = s_unforced = 0;
        s_ownerSeconds = 0.0;
        s_wireBytes = s_changedWords = s_events = 0;
        s_lastWords.clear();
        s_wantPoint = s_shadowStarted = false;
        s_point.reset();
        s_ownerDigests.clear();
        s_variableDiffs.clear();
        s_lastOps.clear();
        s_firstMismatch.reset();
    }
    if (acRequest.contains("arm_like"))
    {
        const auto* pRef = Cast<TESObjectREFR>(TESForm::GetById(acRequest.value("arm_like", 0u)));
        if (!pRef || !pRef->baseForm)
            return {{"ok", false}, {"error", "no such reference"}};
        s_base = pRef->baseForm->formID;
        return {{"ok", true}, {"base", s_base.load()}};
    }
    return {{"ok", true},
            {"base", s_base.load()},
            {"owner", s_ownerId},
            {"shadow", s_shadowId},
            {"ops", s_ops.size()},
            {"owner_updates", s_ownerUpdates},
            {"owner_seconds", s_ownerSeconds},
            {"wire_bytes", s_wireBytes},
            {"changed_words", s_changedWords},
            {"events", s_events},
            {"variables", s_lastWords.size()},
            {"replayed", s_replayed},
            {"equal", s_equal},
            {"variable_diffs", [] {
                 nlohmann::json diffs = nlohmann::json::array();
                 for (const auto& [cIndex, cDiff] : s_variableDiffs)
                     diffs.push_back({{"index", cIndex}, {"updates", cDiff.Updates}, {"first", cDiff.First},
                                      {"owner", std::format("{:08X}", cDiff.Owner)}, {"shadow", std::format("{:08X}", cDiff.Shadow)}});
                 return diffs;
             }()},
            {"dropped_events", s_dropped},
            {"unforced_updates", s_unforced},
            {"first_mismatch", s_firstMismatch ? *s_firstMismatch : nlohmann::json{}}};
}
