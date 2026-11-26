// FakeEngine: actions, graphs and their saved state.
#include "FakeEngine.h"
#include <animation/GraphState.h>
#include <characters/ActorDeathChangeEvent.h>
#include <animation/ActorStateRules.h>
#include <animation/ReplayState.h>
#include <ranges>
#include <core/World.h>

using namespace PartyRules;

// Actor::KillBesideGraph: KillImpl's effects on a copy on its owner's stream, its own death action refused.
void FakeEngine::KillBesideGraph(uint32_t aFormId, const ActionEvent& acAction)
{
    auto& actor = Actors.at(aFormId);
    actor.PoseHeldUntil = 0;
    const bool cWasDead = actor.Dead;
    actor.Dead = true;
    actor.InKillMove = false; // KillImpl 37896 clears flags2 0x4000
    actor.LatestAnimation = acAction;
    if (!cWasDead)
        World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId});
    World::Get().GetDispatcher().trigger(DeathActionReplayedEvent{aFormId, acAction.Tick});
}

void FakeEngine::ReplayDeathAction(uint32_t aFormId, const ActionEvent& acAction)
{
    Actors.at(aFormId).PoseHeldUntil = 0;
    if (!RefuseDeathActions)
        KillActor(aFormId);
    Actors.at(aFormId).LatestAnimation = acAction;
    World::Get().GetDispatcher().trigger(DeathActionReplayedEvent{aFormId, acAction.Tick});
}

bool FakeEngine::PlayReplayed(uint32_t aFormId, const ActionEvent& acAction)
{
    if (::ReplayState::HeldByPair(Actors.at(aFormId).InKillMove, !acAction.TargetEventName.empty()))
        return true;
    Replaying = true;
    const bool cTaken = ReplayAnimation(aFormId, acAction);
    Replaying = false;
    if (acAction.ActionId == kActionGetUp && cTaken)
        StandUp(aFormId);
    ApplyActionWrites(aFormId, acAction.EventName);
    return cTaken;
}

void FakeEngine::ApplyActionWrites(uint32_t aFormId, const std::string& acEvent)
{
    // 39004: HorseEnterInstant seats its actor at once (sit state 3).
    if (acEvent == "HorseEnterInstant")
        Actors.at(aFormId).State1 = (Actors.at(aFormId).State1 & ~(0xFu << kSitShift)) | uint32_t(::ReplayState::kSitSeated) << kSitShift;
    const auto it = ActionWrites.find(acEvent);
    if (it == ActionWrites.end())
        return;
    auto& integers = Actors.at(aFormId).GraphVariables.Integers;
    integers.resize(std::max(integers.size(), it->second.first + 1));
    integers[it->second.first] = uint32_t(it->second.second);
}

void FakeEngine::StopPairClip(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    if (actor.StopsDropped)
    {
        --actor.StopsDropped;
        return;
    }
    actor.InPairClip = false;
    actor.InPair = false;
    actor.PairBlowIn = 0;
    actor.KillActorIn = 0;
    actor.KillMoveEndIn = 0;
}

void FakeEngine::ClipKillActor(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    if (actor.Copy || actor.KillHeld)
    {
        // QueueKill 37316: the kill is a task; KillImpl 37896 counts it when that runs, a frame on.
        actor.PoseHeldUntil = World::Get().GetFrameTick() + ::ReplayState::cPoseHoldMs;
        actor.KillQueued = true;
        return;
    }
    actor.KilledBy = actor.Killer;
    KillActor(aFormId);
}

void FakeEngine::ClipEndsAtBlow(uint32_t aFormId)
{
    EndKillMove(aFormId);
    StopPairClip(aFormId);
    ClipKillActor(aFormId);
}

ActionEvent FakeEngine::GetActorLatestAnimation(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.LatestAnimation : ActionEvent{};
}

namespace
{
// SaveState 63601's layout for one graph ("fake") with one active machine ("Root") in acState.
std::string FakeGraphBlock(const std::string& acState, const FakeGraphMemory& acGraph)
{
    std::string graph;
    const auto cPut = [&graph](const void* apData, size_t aSize) { graph.append(static_cast<const char*>(apData), aSize); };
    const auto cString = [&](std::string_view acText) {
        const auto cLength = uint16_t(acText.size());
        cPut(&cLength, 2);
        cPut(acText.data(), acText.size());
    };
    const uint32_t cOne = 1, cZero = 0;
    cPut(&cOne, 4);
    cString("Root");
    cPut(&cZero, 4);
    cString(acState);
    cPut(&cZero, 4);
    cPut(&cZero, 4);
    // The fake's own: its owner's graph time, which the stream's sync point carries (Skyrim's SaveState has no clock);
    // its clip's progress (-1: none) and whether its exit failed.
    const double cElapsed = acGraph.ClipStartedAt ? acGraph.Timeline - *acGraph.ClipStartedAt : -1.0;
    const double cStuck = acGraph.Stuck ? 1.0 : 0.0;
    cPut(&acGraph.Timeline, 8);
    cPut(&cElapsed, 8);
    cPut(&cStuck, 8);
    std::string block;
    const uint16_t cProject = 4;
    block.append(reinterpret_cast<const char*>(&cProject), 2).append("fake");
    const auto cSize = uint32_t(graph.size());
    block.append(reinterpret_cast<const char*>(&cSize), 4).append(graph);
    return block;
}

// FakeGraphBlock's state and graph time, after the save's 8 bytes of ActorState: none unless it is one graph with one machine.
struct FakeSavedGraph
{
    std::string State;
    double Timeline = 0.0, Elapsed = -1.0;
    bool Stuck = false;
};
std::optional<FakeSavedGraph> FakeGraphState(const std::string& acSaved)
{
    size_t at = 8;
    bool bad = false;
    const auto cTake = [&]<class T>(T) {
        T value{};
        bad = bad || at + sizeof(T) > acSaved.size();
        if (!bad)
            std::memcpy(&value, acSaved.data() + at, sizeof(T));
        at += sizeof(T);
        return value;
    };
    const auto cString = [&] {
        const auto cLength = cTake(uint16_t{});
        bad = bad || at + cLength > acSaved.size();
        std::string text = bad ? std::string{} : acSaved.substr(at, cLength);
        at += cLength;
        return text;
    };
    cString();
    cTake(uint32_t{});
    const auto cMachines = cTake(uint32_t{});
    cString();
    cTake(uint32_t{});
    auto state = cString();
    cTake(uint32_t{});
    cTake(uint32_t{});
    const double cTimeline = cTake(double{});
    const double cElapsed = cTake(double{});
    const double cStuck = cTake(double{});
    if (bad || cMachines != 1 || at != acSaved.size())
        return std::nullopt;
    return FakeSavedGraph{state, cTimeline, cElapsed, cStuck != 0.0};
}
} // namespace

// As Havok's (58372 / 58373): its machine's block as node 0, its blend's chosen child as node 1.
std::string FakeEngine::SaveGraphInternalState(uint32_t aFormId) const
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || it->second.GraphState.empty())
        return {};
    const auto& cGraph = it->second.Graph;
    // GraphInternalState::Take: no point while a cyclic generator cross-blends (mode 2), frozen (1) or following (0).
    const uint8_t cCyclicMode = cGraph.CrossBlendLeftMs > 0.0 ? 2 : cGraph.FrozenParameter ? 1 : 0;
    if (!GraphState::CarriesCyclicMode(cCyclicMode))
        return {};
    GraphState::Node machine{.Id = 0, .State = SaveActorGraphState(aFormId)};
    GraphState::Node blend{.Id = 1, .State = cGraph.BlendState + '\n' + cGraph.Child + '\n' + cGraph.PendingChild};
    GraphState::Node damp{.Id = 2, .State = std::to_string(std::bit_cast<uint32_t>(it->second.Graph.Damped))};
    // The blend node's plain members, beside its internal state (GraphInternalState's members).
    if (cCyclicMode == 2)
        blend.Runtime = "x"; // its mode, a plain member; its effect's blenders are not
    else if (cGraph.FrozenParameter)
        blend.Runtime = std::to_string(*cGraph.FrozenParameter);
    // A clip's local time and its every-N counter: internal state, which the plain graph state leaves out.
    GraphState::Node loop{.Id = 3, .State = std::format("{} {} {}", it->second.LoopFrames, it->second.LoopsSeen, it->second.LoopsToSend)};
    return GraphState::Encode({machine, blend, damp, loop});
}

bool FakeEngine::LoadGraphInternalState(uint32_t aFormId, const std::string& acState)
{
    std::vector<GraphState::Node> nodes;
    if (!Actors.contains(aFormId) || !GraphState::Decode(acState, nodes) || nodes.size() != 4 || !nodes[0].State || !nodes[1].State ||
        !nodes[2].State || !nodes[3].State)
        return false;
    // Havok's internal state holds no actor state: the actor's own stays.
    const uint32_t cState1 = Actors.at(aFormId).State1;
    const bool cLoaded = LoadActorGraphState(aFormId, *nodes[0].State);
    Actors.at(aFormId).State1 = cState1;
    if (!cLoaded)
        return false;
    {
        auto& actor = Actors.at(aFormId);
        std::istringstream loop(*nodes[3].State);
        loop >> actor.LoopFrames >> actor.LoopsSeen >> actor.LoopsToSend;
    }
    auto& graph = Actors.at(aFormId).Graph;
    const auto& cBlend = *nodes[1].State;
    const auto cBreak = cBlend.find('\n');
    const auto cSecond = cBreak == std::string::npos ? std::string::npos : cBlend.find('\n', cBreak + 1);
    graph.BlendState = cBlend.substr(0, cBreak);
    graph.Child = cBreak == std::string::npos ? std::string{} : cBlend.substr(cBreak + 1, cSecond == std::string::npos ? std::string::npos : cSecond - cBreak - 1);
    graph.PendingChild = cSecond == std::string::npos ? std::string{} : cBlend.substr(cSecond + 1);
    graph.ActiveNodesChanged = false; // not node state: Havok's restore walk clears it
    // An active match node's state is in the point; an idle one's is not (58372 walks the active nodes).
    if (const auto cNode = Graph ? Graph->MatchNode(graph.BlendState) : std::nullopt)
    {
        const auto& cChildren = Graph->Matches[*cNode - 4].Children;
        if (const auto it = std::ranges::find(cChildren, graph.Child); it != cChildren.end())
            graph.Matches[*cNode] = int32_t(it - cChildren.begin());
    }
    // Mode 2 on a new clone: its effect has no blenders, and the next active-node walk reads them (63140, 59960).
    const bool cCrossBlending = nodes[1].Runtime == "x";
    RestoredMidCrossBlend += cCrossBlending;
    graph.CrossBlendLeftMs = 0.0;
    graph.FrozenParameter = nodes[1].Runtime.empty() || cCrossBlending ? std::nullopt : std::optional<uint32_t>(uint32_t(std::stoul(nodes[1].Runtime)));
    graph.Damped = std::bit_cast<float>(uint32_t(std::stoul(*nodes[2].State)));
    if (Graph && !graph.History.empty() && graph.History.back().Input == "load")
    {
        graph.History.pop_back(); // its entry with its blend's child
        GraphEntry(Actors.at(aFormId), "load");
    }
    return true;
}

std::string FakeEngine::SaveActorGraphState(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || it->second.GraphState.empty())
        return {};
    const uint32_t cFlags[2] = {it->second.State1, uint32_t(it->second.Weapon) << 5};
    return std::string(reinterpret_cast<const char*>(cFlags), sizeof(cFlags)) + FakeGraphBlock(it->second.GraphState, it->second.Graph);
}

bool FakeEngine::LoadActorGraphState(uint32_t aFormId, const std::string& acState) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    auto& actor = it->second;
    actor.LoopFrames = 0; // clip times are not saved: a loaded graph's clips start over
    // TESObjectREFR::SaveAnimationGraphState prefixes its graph with both ActorState words.
    if (acState.size() >= 8 && FakeGraphState(acState))
    {
        uint32_t flags[2];
        std::memcpy(flags, acState.data(), sizeof(flags));
        actor.State1 = actor.Copy ? ActorStateRules::Take(flags[0], actor.State1, ~ActorStateRules::kPartBits1) : flags[0];
        const uint32_t cFlags2 = actor.Copy ? ActorStateRules::FromSnapshot2(flags[1], uint32_t(actor.Weapon) << 5) : flags[1];
        actor.Weapon = WeaponState((cFlags2 >> 5) & 7);
        // One graph, one active machine: its state is the fake's GraphState.
        const auto cState = FakeGraphState(acState);
        if (!cState)
            return false;
        actor.GraphState = cState->State;
        actor.Graph.Timeline = cState->Timeline;
        actor.Graph.BlendState.clear(); // its generators choose again as they activate
        actor.Graph.Child.clear();
        if (Graph)
        {
            actor.Graph.ClipStartedAt = cState->Elapsed >= 0.0 ? std::optional(cState->Timeline - cState->Elapsed) : std::nullopt;
            actor.Graph.Stuck = cState->Stuck;
        }
    }
    else
        actor.GraphState = acState; // direct fixtures supply the graph alone
    if (Graph)
        GraphEntry(actor, "load");
    return true;
}

bool FakeEngine::LoadGraphStateFlags(uint32_t aFormId, const std::string& acState) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || acState.size() < 8 || !FakeGraphState(acState))
        return false;
    uint32_t flags[2];
    std::memcpy(flags, acState.data(), sizeof(flags));
    it->second.State1 = ActorStateRules::FromSnapshot1(flags[0], it->second.State1);
    it->second.Weapon = WeaponState((ActorStateRules::FromSnapshot2(flags[1], uint32_t(it->second.Weapon) << 5) >> 5) & 7);
    return true;
}

void FakeEngine::SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.LatestAnimation = acAction;
}
