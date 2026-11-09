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
#include <Games/Skyrim/ActionClips.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <snappy.h>
#include <utility>
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
    std::vector<std::pair<uint32_t, uint32_t>> Ext; // the variables changed since the owner's last op ended
};

std::mutex s_lock;
std::atomic<bool> s_armed{false};
std::atomic<uint32_t> s_base{0};
// Over the network: the host records this actor's graph, the guest replays into this copy (the harness carries the ops).
uint32_t s_recordId = 0, s_replayId = 0;
size_t s_taken = 0;                       // host: the ops handed out
uint32_t s_hashesTaken = 0;               // host: the updates whose states were handed out
std::vector<uint32_t> s_takeWords;        // host: the variables of the last op handed out
std::vector<uint32_t> s_feedWords;        // guest: the same, rebuilt
std::map<uint32_t, uint64_t> s_ownerHashes; // guest: the owner's states after each update, hashed
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
// Owner events sent from another thread while its graph updated: their place among the updates is not certain.
std::atomic<uint32_t> s_ownerUpdateThread{0};
uint32_t s_concurrentEvents = 0;
std::vector<std::string> s_concurrentNames;
double s_ownerSeconds = 0.0; // the owner's recorded steps, added up
// What the ops would weigh on the wire, each against the one before: a 3-byte header (kind, step or event length),
// 6 bytes per changed variable (u16 index, u32 value), the event's name.
uint64_t s_wireBytes = 0, s_changedWords = 0, s_events = 0;
std::vector<uint32_t> s_lastWords;
// External mode: an op carries only the variables changed since the owner's last op ended (Ext); what its own update
// wrote, the copy's graph writes too. Weighed as a compact encoding, and Snappy over 0.1 s batches.
bool s_external = false;
std::vector<uint32_t> s_afterWords; // the owner's variables after its last op: what the copy holds before the next
// The shadow's variables after its last replayed op: restored before the next, so only the stream writes them (its own
// channels, 63590, and our snapshot loads are kept out).
std::vector<uint32_t> s_shadowAfter;
std::string s_batch;
double s_batchStart = 0.0;
uint64_t s_compactFull = 0, s_compactExternal = 0, s_snappyExternal = 0, s_externalWords = 0, s_stepBytes = 0, s_seedOps = 0;
// Updates that change nothing (a bit in a frame's bitmap); the external words' bytes XORed against a prediction (a float
// from its last two values, 2a - b; else its last) instead of the copy's value.
uint64_t s_emptyUpdates = 0, s_wordBytesExternal = 0, s_wordBytesPredicted = 0;
std::vector<uint32_t> s_prevWords1, s_prevWords2; // the owner's variables before its last two ops
std::map<uint32_t, std::pair<uint32_t, uint32_t>> s_extTally; // per variable: ops carrying it, their bytes
std::vector<std::string> s_variableNames;                     // the owner's, by index
// The proposed stream encoding (graph-input-stream.md, Weight): steps in a per-frame table shared by every NPC (not
// counted here), an update that changes nothing one bit, otherwise a header byte (event, seed, count), TimeDelta left
// out when it is the step's bits (the copy writes it).
uint64_t s_streamBits = 0, s_streamSnappy = 0, s_timeDeltaSteps = 0, s_timeDeltaOther = 0;
std::string s_streamBatch;
double s_streamBatchStart = 0.0;
int32_t s_timeDeltaIndex = -2; // -2 not looked up, -1 none
hkPseudoRandomGenerator s_lastRandom{};

void PutVarint(std::string& aOut, uint32_t aValue)
{
    for (; aValue >= 0x80; aValue >>= 7)
        aOut.push_back(static_cast<char>(aValue | 0x80));
    aOut.push_back(static_cast<char>(aValue));
}

// Per variable: one byte (index step in the high nibble, 15 = a varint follows; which bytes of value XOR base are
// non-zero in the low), then those bytes.
void PutEntries(std::string& aOut, const std::vector<std::pair<uint32_t, uint32_t>>& acEntries, const std::vector<uint32_t>& acBase)
{
    PutVarint(aOut, static_cast<uint32_t>(acEntries.size()));
    uint32_t last = 0;
    for (const auto& [cIndex, cValue] : acEntries)
    {
        const uint32_t cStep = cIndex - last;
        last = cIndex;
        const uint32_t cXor = cValue ^ (cIndex < acBase.size() ? acBase[cIndex] : 0);
        uint8_t mask = 0;
        for (int b = 0; b < 4; ++b)
            mask |= ((cXor >> (8 * b)) & 0xFF) ? (1 << b) : 0;
        aOut.push_back(static_cast<char>((std::min<uint32_t>(cStep, 15) << 4) | mask));
        if (cStep >= 15)
            PutVarint(aOut, cStep);
        for (int b = 0; b < 4; ++b)
            if (mask & (1 << b))
                aOut.push_back(static_cast<char>(cXor >> (8 * b)));
    }
}

std::vector<std::pair<uint32_t, uint32_t>> Changed(const std::vector<uint32_t>& acWords, const std::vector<uint32_t>& acBase)
{
    std::vector<std::pair<uint32_t, uint32_t>> entries;
    for (size_t i = 0; i < acWords.size(); ++i)
        if (i >= acBase.size() || acBase[i] != acWords[i])
            entries.emplace_back(static_cast<uint32_t>(i), acWords[i]);
    return entries;
}

// A compact op: kind byte (event, seed changed), a 2-byte event id or the 4-byte step, the 8-byte seed if changed.
void PutOp(std::string& aOut, const Op& acOp, bool aSeed, const std::vector<std::pair<uint32_t, uint32_t>>& acEntries,
           const std::vector<uint32_t>& acBase)
{
    aOut.push_back(static_cast<char>((acOp.Event ? 1 : 0) | (aSeed ? 2 : 0)));
    if (acOp.Event)
    {
        const auto cId = static_cast<uint16_t>(std::hash<std::string>{}(acOp.Name));
        aOut.append(reinterpret_cast<const char*>(&cId), 2);
    }
    else
        aOut.append(reinterpret_cast<const char*>(&acOp.Step), 4);
    if (aSeed)
        aOut.append(reinterpret_cast<const char*>(&acOp.Random), 8);
    PutEntries(aOut, acEntries, acBase);
}

uint32_t Predict(uint32_t aLast, uint32_t aBefore)
{
    float last, before;
    std::memcpy(&last, &aLast, 4);
    std::memcpy(&before, &aBefore, 4);
    if (!std::isnormal(last) || !std::isnormal(before))
        return aLast;
    const float cGuess = 2.f * last - before;
    uint32_t guess;
    std::memcpy(&guess, &cGuess, 4);
    return guess;
}

void CountWire(Op& aOp)
{
    uint32_t changed = 0;
    for (size_t i = 0; i < aOp.Words.size(); ++i)
        changed += i >= s_lastWords.size() || s_lastWords[i] != aOp.Words[i];
    s_changedWords += changed;
    s_events += aOp.Event;
    s_wireBytes += 3 + 6 * changed + aOp.Name.size();

    const bool cSeed = std::memcmp(&aOp.Random, &s_lastRandom, sizeof(s_lastRandom)) != 0;
    s_lastRandom = aOp.Random;
    s_seedOps += cSeed;
    s_stepBytes += aOp.Event ? 0 : 4;
    std::string full;
    PutOp(full, aOp, cSeed, Changed(aOp.Words, s_lastWords), s_lastWords);
    s_compactFull += full.size();
    s_lastWords = aOp.Words;

    aOp.Ext = Changed(aOp.Words, s_afterWords);
    s_externalWords += aOp.Ext.size();
    const size_t cBefore = s_batch.size();
    PutOp(s_batch, aOp, cSeed, aOp.Ext, s_afterWords);
    s_compactExternal += s_batch.size() - cBefore;
    if (s_ownerSeconds - s_batchStart >= 0.1)
    {
        std::string packed;
        snappy::Compress(s_batch.data(), s_batch.size(), &packed);
        s_snappyExternal += packed.size();
        s_batch.clear();
        s_batchStart = s_ownerSeconds;
    }
    s_emptyUpdates += !aOp.Event && aOp.Ext.empty() && !cSeed;
    for (const auto& [cIndex, cValue] : aOp.Ext)
    {
        const uint32_t cXor = cValue ^ (cIndex < s_afterWords.size() ? s_afterWords[cIndex] : 0);
        auto& tally = s_extTally[cIndex];
        ++tally.first;
        tally.second += 1 + !!(cXor & 0xFF) + !!(cXor & 0xFF00) + !!(cXor & 0xFF0000) + !!(cXor & 0xFF000000);
    }
    std::string words;
    PutEntries(words, aOp.Ext, s_afterWords);
    s_wordBytesExternal += words.size();
    std::vector<uint32_t> predicted = s_afterWords;
    for (const auto& [cIndex, cValue] : aOp.Ext)
        if (cIndex < s_prevWords1.size() && cIndex < s_prevWords2.size() && cIndex < predicted.size())
            predicted[cIndex] = Predict(s_prevWords1[cIndex], s_prevWords2[cIndex]);
    words.clear();
    PutEntries(words, aOp.Ext, predicted);
    s_wordBytesPredicted += words.size();
    if (s_timeDeltaIndex == -2 && !s_variableNames.empty())
    {
        const auto cIt = std::find(s_variableNames.begin(), s_variableNames.end(), "TimeDelta");
        s_timeDeltaIndex = cIt == s_variableNames.end() ? -1 : static_cast<int32_t>(cIt - s_variableNames.begin());
    }
    std::vector<std::pair<uint32_t, uint32_t>> streamed;
    for (const auto& cEntry : aOp.Ext)
    {
        uint32_t stepBits;
        std::memcpy(&stepBits, &aOp.Step, 4);
        if (!aOp.Event && static_cast<int32_t>(cEntry.first) == s_timeDeltaIndex)
        {
            if (cEntry.second == stepBits)
            {
                ++s_timeDeltaSteps;
                continue;
            }
            ++s_timeDeltaOther;
        }
        streamed.push_back(cEntry);
    }
    if (!aOp.Event && streamed.empty() && !cSeed)
        s_streamBits += 1;
    else
    {
        std::string op;
        op.push_back(static_cast<char>((aOp.Event ? 1 : 0) | (cSeed ? 2 : 0) | (std::min<size_t>(streamed.size(), 63) << 2)));
        if (streamed.size() >= 63)
            PutVarint(op, static_cast<uint32_t>(streamed.size()));
        if (aOp.Event)
            op.append(2, '\0');
        if (cSeed)
            op.append(reinterpret_cast<const char*>(&aOp.Random), 8);
        std::string entries;
        PutEntries(entries, streamed, s_afterWords);
        op.append(entries.begin() + 1, entries.end()); // the header holds the count
        s_streamBits += 8 * op.size();
        s_streamBatch += op;
    }
    if (s_ownerSeconds - s_streamBatchStart >= 0.1)
    {
        std::string packed;
        snappy::Compress(s_streamBatch.data(), s_streamBatch.size(), &packed);
        s_streamSnappy += s_streamBatch.empty() ? 0 : packed.size();
        s_streamBatch.clear();
        s_streamBatchStart = s_ownerSeconds;
    }
    s_prevWords2 = std::move(s_prevWords1);
    s_prevWords1 = aOp.Words;
    s_afterWords = aOp.Words; // an event's own writes go with the next op; an update's are replaced after it runs
}
std::vector<std::string> s_lastOps;
std::optional<nlohmann::json> s_firstMismatch;
thread_local bool s_replaying = false;
thread_local bool s_loading = false; // LoadState's own zero-step update (63602)
// While a probed graph updates on this thread: each range modifier's input as it runs and the ranges it marked.
thread_local std::vector<std::string>* s_rangeLines = nullptr;

// hkbEventsFromRangeModifier::modify (59725).
using TRangeModify = int64_t(hkbEventsFromRangeModifier*, void*);
TRangeModify* RealRangeModify = nullptr;

int64_t HookRangeModify(hkbEventsFromRangeModifier* apNode, void* apContext)
{
    const int64_t cResult = RealRangeModify(apNode, apContext);
    if (s_rangeLines && apNode)
    {
        uint32_t input = 0;
        std::memcpy(&input, &apNode->inputValue, sizeof(input));
        std::string active;
        for (int32_t i = 0; i < apNode->wasActiveInPreviousFrame.size; ++i)
            active += apNode->wasActiveInPreviousFrame.data[i] ? '1' : '0';
        uint32_t lower = 0;
        std::memcpy(&lower, &apNode->lowerBound, sizeof(lower));
        std::string ranges;
        if (apNode->eventRanges)
            for (int32_t i = 0; i < apNode->eventRanges->eventData.size; ++i)
            {
                const auto& cRange = apNode->eventRanges->eventData.data[i];
                uint32_t upper = 0;
                std::memcpy(&upper, &cRange.upperBound, sizeof(upper));
                ranges += std::format(" [{:08X} event {} mode {}]", upper, cRange.eventId, cRange.eventMode);
            }
        s_rangeLines->push_back(std::format("range {} input {:08X} lower {:08X}{} active {}", apNode->name ? apNode->name : "", input, lower,
                                            ranges, active));
    }
    return cResult;
}

Hooks::Initializer s_rangeModifyHook([]() { HOOK_ID(59725, &RealRangeModify, HookRangeModify); });

// Where the shadow starts: the owner's sync point (graph state, seed, every-N counters) right after one of its updates;
// the replay goes on from the next op. Two fresh graphs differ already: activation draws each actor's own seed.
// A modifier's memory (slot 0E), by behaviour and node id: what Skyrim's graph save leaves out.
struct NodeMemory
{
    std::string Key;
    std::string Class;
    hkReferencedObject* State = nullptr;
};

struct StartPoint
{
    uint32_t Seed = 0;
    std::string State;
    std::vector<GraphRandom::Counter> Counters;
    size_t FirstOp = 0;
    std::vector<NodeMemory> Memory; // one game only: the objects themselves
};
uint32_t s_memoryApplied = 0, s_memoryUnmatched = 0;
std::map<std::string, uint32_t> s_memoryClasses;

// Each active modifier, in the graph and the sub-behaviours in it. Only modifiers: a state machine's or a transition
// effect's state points at nodes of its own graph, and the loaded sync point already holds machines and clips.
template <class T> void EachActiveModifier(const hkbBehaviorGraphNode* apGraph, T&& aWork, int aDepth = 0)
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
            EachActiveModifier(static_cast<const hkbBehaviorReferenceGenerator*>(pNode)->behavior, aWork, aDepth + 1);
        else if (cClass.ends_with("Modifier"))
            aWork(*pNode, std::format("{}/{}", apGraph->name ? apGraph->name : "", pNode->id), std::string(cClass));
    }
}

std::vector<NodeMemory> TakeMemory(const BShkbAnimationGraph& acGraph)
{
    std::vector<NodeMemory> memory;
    EachActiveModifier(reinterpret_cast<const hkbBehaviorGraphNode*>(acGraph.behaviorGraph), [&](hkbNode& aNode, std::string aKey, std::string aClass) {
        if (hkReferencedObject* pState = aNode.CreateInternalState())
        {
            aNode.GetInternalState(*pState);
            memory.push_back({std::move(aKey), std::move(aClass), pState});
        }
    });
    return memory;
}

void GiveMemory(BShkbAnimationGraph& aGraph, const std::vector<NodeMemory>& acMemory)
{
    std::map<std::string, const NodeMemory*> byKey;
    for (const auto& cMemory : acMemory)
        byKey[cMemory.Key] = &cMemory;
    EachActiveModifier(reinterpret_cast<const hkbBehaviorGraphNode*>(aGraph.behaviorGraph), [&](hkbNode& aNode, std::string aKey, std::string aClass) {
        const auto it = byKey.find(aKey);
        if (it == byKey.end() || it->second->Class != aClass)
        {
            ++s_memoryUnmatched;
            return;
        }
        aNode.SetInternalState(*it->second->State);
        ++s_memoryApplied;
        ++s_memoryClasses[aClass];
    });
}
bool s_wantPoint = false;
std::optional<StartPoint> s_point;
bool s_shadowStarted = false;
// The shadow's clip files for an event, as its owner's game loads them before the action (39625, ActionClips): a clip
// started without its file lasts 0.0133 s (59263), and its state is left in the same update.
std::shared_ptr<std::atomic<bool>> s_clipsLoading;
size_t s_clipsFor = SIZE_MAX;
uint32_t s_clipWaits = 0, s_clipWaitedFor = 0, s_clipGiveUps = 0;
std::string s_clipGaveUpOn;

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

void WriteChanged(BShkbAnimationGraph& aGraph, const std::vector<std::pair<uint32_t, uint32_t>>& acChanged)
{
    auto* pSet = aGraph.behaviorGraph ? aGraph.behaviorGraph->animationVariables : nullptr;
    if (!pSet || !pSet->data)
        return;
    for (const auto& [cIndex, cValue] : acChanged)
        if (cIndex < pSet->size)
            pSet->data[cIndex] = cValue;
}

// Its playing clips, by node, and whether each has its file (a clip without one lasts 0.0133 s, 59263).
void ActiveClips(const hkbBehaviorGraphNode* apGraph, std::vector<std::string>& aLines, int aDepth = 0)
{
    if (!apGraph || !apGraph->activeNodes || aDepth > 8)
        return;
    const auto& cNodes = *apGraph->activeNodes;
    for (int32_t i = 0; i < cNodes.size; ++i)
    {
        const hkbNode* pNode = cNodes.data[i].nodeClone;
        const hkClass* pClass = pNode ? pNode->GetClassType() : nullptr;
        if (!pClass || !pClass->name || !pNode->name)
            continue;
        const std::string_view cClass = pClass->name;
        if (cClass == "hkbBehaviorReferenceGenerator")
            ActiveClips(static_cast<const hkbBehaviorReferenceGenerator*>(pNode)->behavior, aLines, aDepth + 1);
        else if (cClass == "hkbClipGenerator")
        {
            const auto* pClip = static_cast<const hkbClipGenerator*>(pNode);
            const auto* pBinding = pClip->animationControl ? pClip->animationControl->binding : nullptr;
            aLines.push_back(std::format("clip {}{}", pNode->name, pBinding && pBinding->animation ? "" : " (no file)"));
        }
    }
}

Snapshot Digest(const BShkbAnimationGraph& acGraph, const std::vector<std::string>& acRanges = {})
{
    std::vector<std::string> active = acRanges;
    ActiveClips(reinterpret_cast<const hkbBehaviorGraphNode*>(acGraph.behaviorGraph), active);
    for (const auto& cMachine : TestHarness::EngineCalls::GetGraphMachines(acGraph.holder ? acGraph.holder->formID : 0, ""))
        // Inactive clones too: one resumes its state when its parent activates it again.
        active.push_back(cMachine.Behaviour + "/" + cMachine.Name + "=" + cMachine.State + (cMachine.Active ? "" : " (inactive)"));
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
    if (s_replayId)
    {
        const auto it = s_ownerHashes.find(aUpdate);
        if (it != s_ownerHashes.end() && it->second == std::hash<std::string>{}(acShadow.States))
            ++s_equal;
        else if (!s_firstMismatch)
            s_firstMismatch = nlohmann::json{{"update", aUpdate}, {"shadow", Lines(acShadow.States)}, {"shadow_words", acShadow.Words},
                                             {"last_ops", s_lastOps}};
        return;
    }
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
    if (apRefr && s_recordId && apRefr->formID == s_recordId && !s_owner)
    {
        s_owner = apGraph;
        s_ownerId = s_recordId;
        s_wantPoint = true;
        return Role::kOwner;
    }
    if (apRefr && s_replayId && apRefr->formID == s_replayId && !s_shadow)
    {
        s_shadow = apGraph;
        s_shadowId = s_replayId;
        return Role::kShadow;
    }
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
        if (s_variableNames.empty() && apGraph->behaviorGraph)
        {
            const auto* pBehaviour = reinterpret_cast<const hkbBehaviorGraphNode*>(apGraph->behaviorGraph);
            if (pBehaviour->data && pBehaviour->data->stringData)
                for (int32_t i = 0; i < pBehaviour->data->stringData->variableNames.size; ++i)
                    s_variableNames.emplace_back(pBehaviour->data->stringData->variableNames.data[i] ? pBehaviour->data->stringData->variableNames.data[i] : "?");
        }
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
    s_ownerUpdateThread = GetCurrentThreadId();
    std::vector<std::string> ranges;
    s_rangeLines = &ranges;
    const char cResult = RealUpdate(apGraph, apData, apScratch);
    s_rangeLines = nullptr;
    s_ownerUpdateThread = 0;
    Snapshot digest = Digest(*apGraph, ranges);
    std::optional<StartPoint> point;
    if (s_wantPoint && apGraph->holder)
    {
        point.emplace();
        apGraph->holder->SaveGraphPoint(point->Seed, point->State, point->Counters);
        if (!s_recordId)
            point->Memory = TakeMemory(*apGraph);
    }
    std::vector<uint32_t> after = ReadWords(*apGraph);
    std::scoped_lock _{s_lock};
    s_afterWords = std::move(after);
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
    std::vector<std::pair<size_t, Op>> due;
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
            due.emplace_back(s_cursor, cOp);
        }
    }
    if (start && apGraph->holder)
    {
        s_loading = true;
        apGraph->holder->LoadGraphPoint(start->Seed, start->State);
        apGraph->holder->SetGraphCounters(start->Counters);
        s_loading = false;
        GiveMemory(*apGraph, start->Memory);
        s_shadowAfter = ReadWords(*apGraph);
    }
    char result = 0;
    for (const auto& [cIndex, cOp] : due)
    {
        if (cOp.Event && apGraph->holder)
        {
            if (s_clipsFor != cIndex)
            {
                s_clipsFor = cIndex;
                s_clipWaitedFor = 0;
                s_clipsLoading = ActionClips::Request(*apGraph->holder, cOp.Name.c_str());
            }
            // Not for ever, as ClipWaitOver: a load the process cancels (39258) never says it is in.
            constexpr uint32_t cMaxClipWaits = 120;
            if (s_clipsLoading && !s_clipsLoading->load(std::memory_order_acquire) && ++s_clipWaitedFor > cMaxClipWaits)
            {
                ++s_clipGiveUps;
                s_clipGaveUpOn = cOp.Name;
                s_clipsLoading.reset();
            }
            if (s_clipsLoading && !s_clipsLoading->load(std::memory_order_acquire))
            {
                std::scoped_lock _{s_lock};
                s_cursor = cIndex; // this event and what follows wait for its files
                ++s_clipWaits;
                break;
            }
        }
        // Its seed too: whatever set it since (a replayed action's GraphRandom) is not its owner's input.
        if (apGraph->behaviorGraph && apGraph->behaviorGraph->random)
            *apGraph->behaviorGraph->random = cOp.Random;
        if (s_external)
        {
            WriteWords(*apGraph, s_shadowAfter);
            WriteChanged(*apGraph, cOp.Ext);
        }
        else
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
            std::vector<std::string> ranges;
            s_rangeLines = &ranges;
            result = RealUpdate(apGraph, &data, apScratch);
            s_rangeLines = nullptr;
            Compare(cOp.Update, Digest(*apGraph, ranges));
        }
        if (s_external)
            s_shadowAfter = ReadWords(*apGraph);
        std::scoped_lock _{s_lock};
        s_lastOps.push_back(cOp.Event ? "event " + cOp.Name : std::format("update {} step {:.4f}", cOp.Update, cOp.Step));
        if (s_lastOps.size() > 8)
            s_lastOps.erase(s_lastOps.begin());
    }
    return result;
}

char HookUpdate(BShkbAnimationGraph* apGraph, BSAnimationUpdateData* apData, void* apScratch)
{
    if (!s_armed.load(std::memory_order_relaxed) || !apData || s_loading)
        return RealUpdate(apGraph, apData, apScratch);
    switch (RoleOf(apGraph, apData->refr))
    {
    case Role::kOwner: return RecordOwnerUpdate(apGraph, apData, apScratch);
    case Role::kShadow: return ReplayIntoShadow(apGraph, apData, apScratch);
    default: return RealUpdate(apGraph, apData, apScratch);
    }
}

Hooks::Initializer s_updateHook([]() { HOOK_ID(63587, &RealUpdate, HookUpdate); });

// BShkbAnimationGraph::LoadState (63602): a copy's recovery and sync point loads are not its owner's inputs.
using TLoadState = char(BShkbAnimationGraph*, void*);
TLoadState* RealLoadState = nullptr;

char HookLoadState(BShkbAnimationGraph* apGraph, void* apStream)
{
    if (s_armed.load(std::memory_order_relaxed) && !s_loading)
    {
        std::scoped_lock _{s_lock};
        if (apGraph == s_shadow)
            return 0;
    }
    return RealLoadState(apGraph, apStream);
}

Hooks::Initializer s_loadStateHook([]() { HOOK_ID(63602, &RealLoadState, HookLoadState); });

std::string ToHex(const std::string& acBytes)
{
    std::string hex;
    for (const unsigned char cByte : acBytes)
        hex += std::format("{:02X}", cByte);
    return hex;
}

std::string FromHex(const std::string& acHex)
{
    std::string bytes;
    for (size_t i = 0; i + 1 < acHex.size(); i += 2)
        bytes.push_back(static_cast<char>(std::stoi(acHex.substr(i, 2), nullptr, 16)));
    return bytes;
}

// Host: the ops, states and sync point not handed out yet; variables against the last op handed out.
nlohmann::json Take()
{
    nlohmann::json reply = {{"ok", true}, {"owner_updates", s_ownerUpdates}, {"owner_seconds", s_ownerSeconds}, {"wire_bytes", s_wireBytes},
                            {"concurrent_events", s_concurrentEvents}, {"concurrent_names", s_concurrentNames}};
    if (!s_point)
        return reply;
    if (s_taken < s_point->FirstOp)
    {
        s_taken = s_point->FirstOp;
        s_hashesTaken = s_point->FirstOp ? s_ops[s_point->FirstOp - 1].Update + 1 : 0;
        nlohmann::json counters = nlohmann::json::array();
        for (const auto& cCounter : s_point->Counters)
            counters.push_back({cCounter.Name, cCounter.Seen, cCounter.Needed});
        reply["point"] = {{"seed", s_point->Seed}, {"state", ToHex(s_point->State)}, {"counters", counters}};
    }
    nlohmann::json ops = nlohmann::json::array();
    for (; s_taken < s_ops.size(); ++s_taken)
    {
        const Op& cOp = s_ops[s_taken];
        nlohmann::json words = nlohmann::json::array();
        for (size_t i = 0; i < cOp.Words.size(); ++i)
            if (i >= s_takeWords.size() || s_takeWords[i] != cOp.Words[i])
                words.push_back({i, cOp.Words[i]});
        s_takeWords = cOp.Words;
        nlohmann::json op = {{"u", cOp.Update}, {"r", {cOp.Random.seed, cOp.Random.current}}, {"w", words}, {"n", cOp.Words.size()}};
        if (s_external)
            op["x"] = cOp.Ext;
        if (cOp.Event)
            op["e"] = cOp.Name;
        else
            op["s"] = cOp.Step;
        ops.push_back(std::move(op));
    }
    reply["ops"] = std::move(ops);
    nlohmann::json hashes = nlohmann::json::array();
    for (; s_hashesTaken < s_ownerUpdates && s_hashesTaken < s_ownerDigests.size(); ++s_hashesTaken)
        hashes.push_back({s_hashesTaken, std::format("{:016X}", std::hash<std::string>{}(s_ownerDigests[s_hashesTaken].States))});
    reply["hashes"] = std::move(hashes);
    return reply;
}

// Guest: what the host handed out, in order.
void Feed(const nlohmann::json& acTaken)
{
    if (acTaken.contains("point") && !s_point)
    {
        StartPoint point;
        point.Seed = acTaken["point"].value("seed", 0u);
        point.State = FromHex(acTaken["point"].value("state", std::string{}));
        for (const auto& cCounter : acTaken["point"].value("counters", nlohmann::json::array()))
            point.Counters.push_back({cCounter[0].get<std::string>(), cCounter[1].get<int32_t>(), cCounter[2].get<int32_t>()});
        point.FirstOp = s_ops.size();
        s_point = std::move(point);
    }
    for (const auto& cOp : acTaken.value("ops", nlohmann::json::array()))
    {
        s_feedWords.resize(cOp.value("n", size_t{0}));
        for (const auto& cWord : cOp["w"])
            if (cWord[0].get<size_t>() < s_feedWords.size())
                s_feedWords[cWord[0].get<size_t>()] = cWord[1].get<uint32_t>();
        Op op{cOp.contains("e"), cOp.value("e", std::string{}), cOp.value("s", 0.f), cOp.value("u", 0u),
              {cOp["r"][0].get<uint32_t>(), cOp["r"][1].get<uint32_t>()}, s_feedWords};
        if (cOp.contains("x"))
            op.Ext = cOp["x"].get<std::vector<std::pair<uint32_t, uint32_t>>>();
        s_ops.push_back(std::move(op));
    }
    for (const auto& cHash : acTaken.value("hashes", nlohmann::json::array()))
    {
        const uint32_t cUpdate = cHash[0].get<uint32_t>();
        s_ownerHashes[cUpdate] = std::stoull(cHash[1].get<std::string>(), nullptr, 16);
        s_ownerUpdates = std::max(s_ownerUpdates, cUpdate + 1);
    }
}
} // namespace

bool GraphReplay::DropEvent(BShkbAnimationGraph* apGraph, const BSFixedString* apEvent) noexcept
{
    if (!s_armed.load(std::memory_order_relaxed) || s_replaying)
        return false;
    std::scoped_lock _{s_lock};
    if (apGraph == s_shadow)
    {
        ++s_dropped;
        return true;
    }
    if (apGraph == s_owner)
    {
        Op op{true, apEvent && apEvent->data ? apEvent->data : "", 0.f, s_ownerUpdates, {}, ReadWords(*apGraph)};
        if (const uint32_t cUpdating = s_ownerUpdateThread.load(); cUpdating && cUpdating != GetCurrentThreadId())
        {
            ++s_concurrentEvents;
            if (s_concurrentNames.size() < 10)
                s_concurrentNames.push_back(op.Name);
        }
        if (apGraph->behaviorGraph && apGraph->behaviorGraph->random)
            op.Random = *apGraph->behaviorGraph->random;
        CountWire(op);
        s_ops.push_back(std::move(op));
    }
    return false;
}

nlohmann::json GraphReplay::Command(const nlohmann::json& acRequest) noexcept
{
    std::scoped_lock _{s_lock};
    if (acRequest.value("take", false))
        return Take();
    if (acRequest.contains("feed"))
    {
        Feed(acRequest["feed"]);
        return {{"ok", true}, {"replayed", s_replayed}, {"equal", s_equal}, {"known_updates", s_ownerUpdates}};
    }
    if (acRequest.contains("states_at"))
    {
        const uint32_t cUpdate = acRequest.value("states_at", 0u);
        const bool cKnown = cUpdate < s_ownerDigests.size();
        return {{"ok", cKnown}, {"states", cKnown ? Lines(s_ownerDigests[cUpdate].States) : std::vector<std::string>{}},
                {"words", cKnown ? s_ownerDigests[cUpdate].Words : std::vector<uint32_t>{}}};
    }
    const bool cArm = acRequest.contains("arm_like") || acRequest.contains("record") || acRequest.contains("replay");
    if (acRequest.value("stop", false) || cArm)
    {
        s_armed = false;
        s_base = 0;
        s_recordId = s_replayId = 0;
        s_taken = 0;
        s_hashesTaken = 0;
        s_takeWords.clear();
        s_feedWords.clear();
        s_ownerHashes.clear();
        s_owner = s_shadow = nullptr;
        s_ownerId = s_shadowId = 0;
        s_ops.clear();
        s_cursor = s_ownerUpdates = s_replayed = s_equal = s_dropped = s_unforced = 0;
        s_ownerSeconds = 0.0;
        s_wireBytes = s_changedWords = s_events = 0;
        s_lastWords.clear();
        s_external = acRequest.value("external", false);
        s_afterWords.clear();
        s_shadowAfter.clear();
        s_batch.clear();
        s_batchStart = 0.0;
        s_compactFull = s_compactExternal = s_snappyExternal = s_externalWords = s_stepBytes = s_seedOps = 0;
        s_emptyUpdates = s_wordBytesExternal = s_wordBytesPredicted = 0;
        s_prevWords1.clear();
        s_prevWords2.clear();
        s_extTally.clear();
        s_variableNames.clear();
        s_streamBits = s_streamSnappy = s_timeDeltaSteps = s_timeDeltaOther = 0;
        s_streamBatch.clear();
        s_streamBatchStart = 0.0;
        s_timeDeltaIndex = -2;
        s_lastRandom = {};
        s_wantPoint = s_shadowStarted = false;
        s_concurrentEvents = 0;
        s_memoryApplied = s_memoryUnmatched = 0;
        s_memoryClasses.clear();
        s_concurrentNames.clear();
        s_clipsLoading.reset();
        s_clipsFor = SIZE_MAX;
        s_clipWaits = s_clipWaitedFor = s_clipGiveUps = 0;
        s_clipGaveUpOn.clear();
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
        s_armed = true;
        return {{"ok", true}, {"base", s_base.load()}};
    }
    if (acRequest.contains("record") || acRequest.contains("replay"))
    {
        s_recordId = acRequest.value("record", 0u);
        s_replayId = acRequest.value("replay", 0u);
        s_armed = true;
        return {{"ok", true}};
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
            {"external", s_external},
            {"compact_full_bytes", s_compactFull},
            {"compact_external_bytes", s_compactExternal},
            {"snappy_external_bytes", s_snappyExternal},
            {"external_words", s_externalWords},
            {"step_bytes", s_stepBytes},
            {"seed_ops", s_seedOps},
            {"empty_updates", s_emptyUpdates},
            {"stream_bytes", s_streamBits / 8},
            {"stream_snappy_bytes", s_streamSnappy},
            {"time_delta_is_step", s_timeDeltaSteps},
            {"time_delta_other", s_timeDeltaOther},
            {"ext_top", [] {
                 std::vector<std::pair<uint32_t, uint32_t>> rows; // bytes, index
                 for (const auto& [cIndex, cTally] : s_extTally)
                     rows.emplace_back(cTally.second, cIndex);
                 std::sort(rows.rbegin(), rows.rend());
                 nlohmann::json top = nlohmann::json::array();
                 for (size_t i = 0; i < std::min<size_t>(rows.size(), 16); ++i)
                 {
                     const uint32_t cIndex = rows[i].second;
                     top.push_back({cIndex < s_variableNames.size() ? s_variableNames[cIndex] : std::to_string(cIndex),
                                    s_extTally[cIndex].first, rows[i].first});
                 }
                 return top;
             }()},
            {"word_bytes_external", s_wordBytesExternal},
            {"word_bytes_predicted", s_wordBytesPredicted},
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
            {"started", s_shadowStarted},
            {"clip_waits", s_clipWaits},
            {"memory_applied", s_memoryApplied},
            {"memory_unmatched", s_memoryUnmatched},
            {"memory_classes", s_memoryClasses},
            {"clip_give_ups", s_clipGiveUps},
            {"clip_gave_up_on", s_clipGaveUpOn},
            {"known_updates", s_ownerHashes.size()},
            {"first_mismatch", s_firstMismatch ? *s_firstMismatch : nlohmann::json{}}};
}
