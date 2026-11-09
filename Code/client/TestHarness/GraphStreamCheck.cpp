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
#include <deque>
#include <map>
#include <mutex>
#include <unordered_map>

namespace
{
using TUpdate = char(BShkbAnimationGraph*, BSAnimationUpdateData*, void*);
TUpdate* RealUpdate = nullptr;

struct Hashes
{
    std::optional<uint32_t> Last;                        // the update last hashed
    std::deque<std::pair<uint32_t, uint64_t>> Fresh;     // not taken yet
    std::map<uint32_t, std::string> Recent;              // the states behind them, for a difference's report
};
std::mutex s_lock;
std::unordered_map<uint32_t, Hashes> s_actors;
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
    return cResult;
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
}

nlohmann::json GraphStreamCheck::Command(const nlohmann::json& acRequest) noexcept
{
    auto& streams = World::Get().GetGraphStreams();
    std::scoped_lock _{s_lock};
    if (acRequest.contains("states_at"))
    {
        const auto& cAt = acRequest["states_at"];
        const auto it = s_actors.find(cAt.value("form", 0u));
        if (it == s_actors.end())
            return {{"ok", false}};
        const auto cStates = it->second.Recent.find(cAt.value("update", 0u));
        if (cStates == it->second.Recent.end())
            return {{"ok", false}};
        return {{"ok", true}, {"states", Lines(cStates->second)}};
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
