#include "GraphStreamCheck.h"
#include "EngineCalls.h"
#include <Actor.h>
#include <Games/Animation/BSAnimationUpdateData.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbNode.h>
#include <Hooks/FunctionHook.h>
#include <World.h>
#include <algorithm>
#include <bit>
#include <deque>
#include <map>
#include <mutex>
#include <unordered_map>

namespace
{
using TUpdate = char(BShkbAnimationGraph*, BSAnimationUpdateData*, void*);
TUpdate* RealUpdate = nullptr;
using TSendEvent = bool(BShkbAnimationGraph*, const BSFixedString*);
TSendEvent* RealSendEvent = nullptr;

struct Hashes
{
    std::optional<uint32_t> Last;                        // the update last hashed
    std::deque<std::pair<uint32_t, uint64_t>> Fresh;     // not taken yet
    std::map<uint32_t, std::string> Recent;              // the states behind them, for a difference's report
    std::vector<std::string> Events;                     // the events its graph handled since its last update
    std::map<uint32_t, std::vector<std::string>> EventsBefore; // by update, for a difference's report
    std::map<uint32_t, std::pair<std::vector<uint32_t>, std::vector<uint32_t>>> Words; // its variables before and after
    std::map<uint32_t, std::string> Times;                // its clips' local times after each update (reported, not compared)
    std::vector<std::string> Handled;                     // every event its graph handled since its last update, by sender
    std::map<uint32_t, std::vector<std::string>> HandledBefore;
    std::optional<float> Step;                            // its behaviour graph's step in this update (58329)
    std::string InUpdate;                                 // the variables its behaviour update itself wrote
    std::map<uint32_t, std::string> Steps;                // each update's step, LOD and countdowns (reported, not compared)
};
std::mutex s_lock;
std::unordered_map<uint32_t, Hashes> s_actors;
std::unordered_map<const void*, uint32_t> s_graphs; // a streamed actor's behaviour graph, to its form id
std::unordered_map<uint32_t, std::vector<std::string>> s_names; // its variables' names

// hkbBehaviorGraph::HandleEventOnActiveNodes (58377): (graph, context, event { id, payload +8, sender +0x10 }): every
// event a graph handles, its own raised ones too (handleEvents 58344), with the node that raised it.
using THandleEvent = void(void*, void*, const uint8_t*);
THandleEvent* RealHandleEvent = nullptr;
// hkbBehaviorGraph's update (vfunc 5, 58329): (graph, context, step), the step 63587 chose.
using TBehaviourUpdate = void(void*, void*, float);
TBehaviourUpdate* RealBehaviourUpdate = nullptr;

void HookBehaviourUpdate(void* apGraph, void* apContext, float aStep)
{
    std::optional<uint32_t> formId;
    {
        std::scoped_lock _{s_lock};
        if (const auto it = s_graphs.find(apGraph); it != s_graphs.end())
            formId = it->second;
    }
    if (!formId)
        return RealBehaviourUpdate(apGraph, apContext, aStep);
    const auto* pSet = static_cast<hkbBehaviorGraph*>(apGraph)->animationVariables;
    const auto cRead = [pSet] { return pSet && pSet->data ? std::vector<uint32_t>(pSet->data, pSet->data + pSet->size) : std::vector<uint32_t>{}; };
    const auto cBefore = cRead();
    RealBehaviourUpdate(apGraph, apContext, aStep);
    const auto cAfter = cRead();
    std::scoped_lock _{s_lock};
    auto& hashes = s_actors[*formId];
    hashes.Step = aStep;
    const auto cNames = s_names.find(*formId);
    for (size_t i = 0; i < std::min(cBefore.size(), cAfter.size()); ++i)
        if (cBefore[i] != cAfter[i])
            hashes.InUpdate += std::format(" {}:{:08X}>{:08X}", cNames != s_names.end() && i < cNames->second.size() ? cNames->second[i] : std::to_string(i),
                                           cBefore[i], cAfter[i]);
}

std::vector<uint32_t> GraphWords(const BShkbAnimationGraph& acGraph)
{
    const auto* pSet = acGraph.behaviorGraph ? acGraph.behaviorGraph->animationVariables : nullptr;
    return pSet && pSet->data ? std::vector<uint32_t>(pSet->data, pSet->data + pSet->size) : std::vector<uint32_t>{};
}

// Its variables' names (the root behaviour's string data), for a difference's report.
std::vector<std::string> VariableNames(const BShkbAnimationGraph& acGraph)
{
    std::vector<std::string> names;
    const auto* pGraph = reinterpret_cast<const hkbBehaviorGraphNode*>(acGraph.behaviorGraph);
    if (!pGraph || !pGraph->data || !pGraph->data->stringData)
        return names;
    const auto& cNames = pGraph->data->stringData->variableNames;
    for (int32_t i = 0; i < cNames.size; ++i)
        names.emplace_back(cNames.data[i] ? cNames.data[i] : "");
    return names;
}

void HookHandleEvent(void* apGraph, void* apContext, const uint8_t* acpEvent)
{
    RealHandleEvent(apGraph, apContext, acpEvent);
    if (!apGraph || !acpEvent)
        return;
    std::scoped_lock _{s_lock};
    const auto it = s_graphs.find(apGraph);
    if (it == s_graphs.end())
        return;
    const int32_t cId = *reinterpret_cast<const int32_t*>(acpEvent);
    const auto* pSender = *reinterpret_cast<const hkbNode* const*>(acpEvent + 0x10);
    const auto* pGraph = static_cast<const hkbBehaviorGraphNode*>(apGraph);
    const auto* pNames = pGraph->data && pGraph->data->stringData ? &pGraph->data->stringData->eventNames : nullptr;
    const char* cpName = pNames && cId >= 0 && cId < pNames->size ? pNames->data[cId] : nullptr;
    const char* cpSender = pSender && pSender->name ? reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(pSender->name) & ~uintptr_t(1)) : nullptr;
    auto& handled = s_actors[it->second].Handled;
    if (handled.size() < 64)
        handled.push_back(std::format("{} <- {}", cpName ? cpName : std::to_string(cId), cpSender ? cpSender : "outside"));
}
bool s_installed = false;

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

// Its active clips' local times, exact (hkbClipGenerator +0xF0).
std::string ClipTimes(const hkbBehaviorGraphNode* apGraph)
{
    std::string times;
    if (!apGraph || !apGraph->activeNodes)
        return times;
    for (int32_t i = 0; i < apGraph->activeNodes->size; ++i)
    {
        const hkbNode* pNode = apGraph->activeNodes->data[i].nodeClone;
        const hkClass* pClass = pNode ? pNode->GetClassType() : nullptr;
        if (pClass && pClass->name && std::string_view(pClass->name) == "hkbClipGenerator" && pNode->name)
            times += std::format("{}={:.4f}({:08X}) ", pNode->name, static_cast<const hkbClipGenerator*>(pNode)->localTime,
                                 std::bit_cast<uint32_t>(static_cast<const hkbClipGenerator*>(pNode)->localTime));
    }
    return times;
}

// What must match: its machines' states (inactive clones too: one resumes its state when its parent activates it
// again) and its clips. Variables can differ where a node reads the world (foot IK, look-at).
std::string States(const BShkbAnimationGraph& acGraph)
{
    std::vector<std::string> lines;
    ActiveClips(reinterpret_cast<const hkbBehaviorGraphNode*>(acGraph.behaviorGraph), lines);
    for (const auto& cMachine : TestHarness::EngineCalls::GetGraphMachines(acGraph.holder ? acGraph.holder->formID : 0, ""))
        lines.push_back(cMachine.Behaviour + "/" + cMachine.Name + "=" + cMachine.State + (cMachine.Active ? "" : " (inactive)"));
    std::ranges::sort(lines);
    std::string states;
    for (const auto& cLine : lines)
        states += (states.empty() ? "" : "\n") + cLine;
    return states;
}

// Beneath the stream's hook: after each real update of a streamed graph, at an update index it has not hashed.
char HookUpdate(BShkbAnimationGraph* apGraph, BSAnimationUpdateData* apData, void* apScratch)
{
    auto before = apGraph && apGraph->behaviorGraph ? GraphWords(*apGraph) : std::vector<uint32_t>{};
    const char cResult = RealUpdate(apGraph, apData, apScratch);
    if (!apGraph || !apGraph->holder || !apGraph->behaviorGraph)
        return cResult;
    const uint32_t cFormId = apGraph->holder->formID;
    // Lock-free readouts only: a copy's replay holds its stream's lock around this call.
    const auto cUpdate = World::Get().GetGraphStreams().UpdateIndex(cFormId);
    if (!cUpdate)
        return cResult;
    {
        std::scoped_lock _{s_lock};
        if (s_actors[cFormId].Last == cUpdate)
            return cResult; // the same update again: a load's zero-step one, or its own update off the stream
    }
    std::string states = States(*apGraph);
    const uint64_t cHash = std::hash<std::string>{}(states);
    std::scoped_lock _{s_lock};
    auto& hashes = s_actors[cFormId];
    hashes.Last = cUpdate;
    hashes.Fresh.emplace_back(*cUpdate, cHash);
    while (hashes.Fresh.size() > 20000)
        hashes.Fresh.pop_front();
    hashes.Recent[*cUpdate] = std::move(states);
    while (hashes.Recent.size() > 2000)
        hashes.Recent.erase(hashes.Recent.begin());
    hashes.Words[*cUpdate] = {std::move(before), GraphWords(*apGraph)};
    hashes.Times[*cUpdate] = ClipTimes(reinterpret_cast<const hkbBehaviorGraphNode*>(apGraph->behaviorGraph));
    while (hashes.Times.size() > 2000)
        hashes.Times.erase(hashes.Times.begin());
    const auto cStep = std::exchange(hashes.Step, std::nullopt);
    hashes.Steps[*cUpdate] = std::format("step {} dt {:.4f} force {:d} lod {} countdown {:.4f}/{:.4f}; in update{}",
                                         cStep ? std::format("{:.4f}({:08X})", *cStep, std::bit_cast<uint32_t>(*cStep)) : "none",
                                         apData ? apData->deltaTime : 0.f, apData && apData->force, apGraph->lod,
                                         apGraph->interpCountdown[0], apGraph->interpCountdown[1], std::exchange(hashes.InUpdate, {}));
    while (hashes.Steps.size() > 2000)
        hashes.Steps.erase(hashes.Steps.begin());
    while (hashes.Words.size() > 2000)
        hashes.Words.erase(hashes.Words.begin());
    if (!s_names.contains(cFormId))
        s_names[cFormId] = VariableNames(*apGraph);
    if (!hashes.Events.empty())
        hashes.EventsBefore[*cUpdate] = std::exchange(hashes.Events, {});
    if (!hashes.Handled.empty())
        hashes.HandledBefore[*cUpdate] = std::exchange(hashes.Handled, {});
    while (hashes.HandledBefore.size() > 500)
        hashes.HandledBefore.erase(hashes.HandledBefore.begin());
    s_graphs[apGraph->behaviorGraph] = cFormId;
    while (hashes.EventsBefore.size() > 500)
        hashes.EventsBefore.erase(hashes.EventsBefore.begin());
    return cResult;
}

// Beneath the stream's 63591 hook: the events a streamed graph handles (a copy's own never get here) and whether it
// took them.
bool HookSendEvent(BShkbAnimationGraph* apGraph, const BSFixedString* apEvent)
{
    const bool cTaken = RealSendEvent(apGraph, apEvent);
    if (!apGraph || !apGraph->holder || !apEvent || !apEvent->data)
        return cTaken;
    const uint32_t cFormId = apGraph->holder->formID;
    if (!World::Get().GetGraphStreams().UpdateIndex(cFormId))
        return cTaken;
    std::scoped_lock _{s_lock};
    auto& events = s_actors[cFormId].Events;
    if (events.size() < 64)
        events.push_back(std::format("{} {}", apEvent->data, cTaken ? "taken" : "refused"));
    return cTaken;
}

std::vector<std::string> Lines(const std::string& acStates)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < acStates.size())
    {
        const size_t cEnd = std::min(acStates.find('\n', start), acStates.size());
        lines.push_back(acStates.substr(start, cEnd - start));
        start = cEnd + 1;
    }
    return lines;
}
} // namespace

void GraphStreamCheck::Install() noexcept
{
    if (std::exchange(s_installed, true))
        return;
    Hooks::Resolve<63587>(&RealUpdate);
    if (!Hooks::FunctionHookManager::GetInstance().AddChained(
            Hooks::FunctionHook(reinterpret_cast<void**>(&RealUpdate), reinterpret_cast<void*>(HookUpdate), "GraphStreamCheck")))
        spdlog::warn("GraphStreamCheck: could not chain under the graph update hook");
    Hooks::Resolve<63591>(&RealSendEvent);
    if (!Hooks::FunctionHookManager::GetInstance().AddChained(
            Hooks::FunctionHook(reinterpret_cast<void**>(&RealSendEvent), reinterpret_cast<void*>(HookSendEvent), "GraphStreamCheckEvents")))
        spdlog::warn("GraphStreamCheck: could not chain under the graph event hook");
    Hooks::Resolve<58329>(&RealBehaviourUpdate);
    if (!Hooks::FunctionHookManager::GetInstance().AddChained(
            Hooks::FunctionHook(reinterpret_cast<void**>(&RealBehaviourUpdate), reinterpret_cast<void*>(HookBehaviourUpdate), "GraphStreamCheckSteps")))
        spdlog::warn("GraphStreamCheck: could not chain under the behaviour update hook");
    Hooks::Resolve<58377>(&RealHandleEvent);
    if (!Hooks::FunctionHookManager::GetInstance().AddChained(
            Hooks::FunctionHook(reinterpret_cast<void**>(&RealHandleEvent), reinterpret_cast<void*>(HookHandleEvent), "GraphStreamCheckHandled")))
        spdlog::warn("GraphStreamCheck: could not hook the graph's event handling");
}

nlohmann::json GraphStreamCheck::Command(const nlohmann::json& acRequest) noexcept
{
    auto& streams = World::Get().GetGraphStreams();
    // A copy's stream started over, as a copy arriving mid-loop starts: it asks its owner for a new sync point.
    if (acRequest.contains("restart"))
    {
        streams.Forget(acRequest["restart"].get<uint32_t>());
        return {{"ok", true}};
    }
    std::scoped_lock _{s_lock};
    if (acRequest.contains("states_at"))
    {
        const auto& cAt = acRequest["states_at"];
        const auto it = s_actors.find(cAt.value("form", 0u));
        if (it == s_actors.end())
            return {{"ok", false}};
        const uint32_t cUpdate = cAt.value("update", 0u);
        const auto cStates = it->second.Recent.find(cUpdate);
        if (cStates == it->second.Recent.end())
            return {{"ok", false}};
        // The events before the updates leading to it, for what made it differ.
        nlohmann::json events = nlohmann::json::object();
        for (auto e = it->second.EventsBefore.lower_bound(cUpdate >= 20 ? cUpdate - 20 : 0); e != it->second.EventsBefore.end() && e->first <= cUpdate; ++e)
            events[std::to_string(e->first)] = e->second;
        nlohmann::json handled = nlohmann::json::object();
        for (auto e = it->second.HandledBefore.lower_bound(cAt.value("handled_from", cUpdate >= 5 ? cUpdate - 5 : 0u)); e != it->second.HandledBefore.end() && e->first <= cUpdate; ++e)
            handled[std::to_string(e->first)] = e->second;
        nlohmann::json times = nlohmann::json::object();
        const uint32_t cFrom = cAt.value("times_from", cUpdate >= 3 ? cUpdate - 3 : 0u);
        for (auto e = it->second.Times.lower_bound(cFrom); e != it->second.Times.end() && e->first <= cUpdate; ++e)
            times[std::to_string(e->first)] = e->second;
        nlohmann::json steps = nlohmann::json::object();
        for (auto e = it->second.Steps.lower_bound(cAt.value("steps_from", cFrom)); e != it->second.Steps.end() && e->first <= cUpdate; ++e)
            steps[std::to_string(e->first)] = e->second;
        nlohmann::json words = nlohmann::json::object();
        if (const auto cWords = it->second.Words.find(cUpdate); cWords != it->second.Words.end())
            words = {{"before", cWords->second.first}, {"after", cWords->second.second}};
        // Every update's variables from words_from on (to find where a copy's first parted).
        nlohmann::json wordsFrom = nlohmann::json::object();
        if (cAt.contains("words_from"))
            for (auto e = it->second.Words.lower_bound(cAt["words_from"].get<uint32_t>()); e != it->second.Words.end() && e->first <= cUpdate; ++e)
                wordsFrom[std::to_string(e->first)] = {e->second.first, e->second.second};
        const auto cNames = s_names.find(cAt.value("form", 0u));
        return {{"ok", true}, {"states", Lines(cStates->second)}, {"events", events}, {"words", words}, {"words_from", wordsFrom}, {"handled", handled}, {"times", times}, {"steps", steps},
                {"names", cNames != s_names.end() ? nlohmann::json(cNames->second) : nlohmann::json::array()}};
    }
    const bool cTake = acRequest.value("take", false);
    nlohmann::json actors = nlohmann::json::array();
    for (auto& [cFormId, hashes] : s_actors)
    {
        const bool cOwner = streams.Recording(cFormId);
        nlohmann::json actor = {{"form", cFormId},
                                {"actor", streams.ActorIdOf(cFormId)},
                                {"role", cOwner ? "owner" : "copy"},
                                {"on_stream", cOwner || streams.OnStream(cFormId)},
                                {"last", hashes.Last ? nlohmann::json(*hashes.Last) : nlohmann::json()}};
        if (cTake)
        {
            nlohmann::json taken = nlohmann::json::array();
            for (const auto& [cUpdate, cHash] : hashes.Fresh)
                taken.push_back({cUpdate, std::format("{:016X}", cHash)});
            hashes.Fresh.clear();
            actor["hashes"] = std::move(taken);
        }
        actors.push_back(std::move(actor));
    }
    return {{"ok", true}, {"actors", actors}};
}
