#include <Diagnostics/HeadTrackRead.h>
#include <Games/Skyrim/CopyCombat.h>
#include <Events/MenuEvent.h>
#include <Events/ActorHeadUpdateEvent.h>
#include <Games/CheckedPlacement.h>
#include <Events/PlayerDeathEvent.h>
#include "FakeEngine.h"

#include <Games/Animation/GraphState.h>
#include <Games/Animation/GraphStream.h>
#include <bit>
#include <set>
#include <span>
#include <Events/ActorRenamedEvent.h>
#include "FakeDialogue.h"
#include <Events/PlayerTravelQueuedEvent.h>
#include <Events/StoryEvents.h>

#include <Events/ContainerChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Services/ContainerOpens.h>
#include <Services/DeathDrops.h>
#include <Events/AddTargetEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ActorDamagedEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Events/NpcHeldEvent.h>
#include <Games/GuestNpcs.h>
#include <Games/DeathActions.h>
#include <Games/Animation/ReplayCapture.h>
#include <Games/Animation/ReplayGraph.h>
#include <Games/NpcSnapshot.h>
#include <Events/HitEvent.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Events/SpellCastEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/LooseItemEvents.h>
#include <Events/CallEndedEvent.h>
#include <Events/RoutedCallEvent.h>
#include <Events/PlayerProgressEvent.h>
#include <Games/EngineNames.h>
#include <Games/Animation/ReplayState.h>
#include <Events/RemoveWaypointEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <Papyrus/PartyNatives.h>
#include <Papyrus/SystemQuests.h>
#include <Events/SystemQuestLearnedEvent.h>
#include <ScriptValues.h>
#include <array>
#include <ranges>
#include <cstdio>
#include <Games/Overrides.h>
#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Services/SaveService.h>
#include <Services/SaveNames.h>
#include <Wire/Buffer.h>
#include <World.h>
#include <FormIds.h>

using namespace PartyRules;
bool FakeEngine::IsLyingInRagdoll(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    if (it->second.Copy)
        return RagdollPosed(aFormId);
    return it->second.KnockedDown || std::ranges::any_of(Bodies, [&](const auto& acBody) {
        return acBody.second.RagdollOf == aFormId && ((acBody.second.Awake && !it->second.Rising) || it->second.Dead);
    });
}

bool FakeEngine::RagdollPosed(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !it->second.Copy)
        return false;
    return it->second.KnockedDown ||
           (it->second.Dead && std::ranges::any_of(Bodies, [&](const auto& acBody) { return acBody.second.RagdollOf == aFormId && acBody.second.Held; }));
}

void FakeEngine::ShovePlayer() noexcept
{
    auto& player = Actors.at(kPlayer);
    const auto cPasses = [this](uint32_t aFormId) { return std::ranges::find(PassingThrough, aFormId) != PassingThrough.end(); };
    for (const auto& [cId, cActor] : Actors)
    {
        if (!cActor.Copy || cActor.Player || !cActor.Stepped || cActor.Dead || cActor.Disabled || !cActor.Loaded || cActor.Cell != player.Cell)
            continue;
        if (std::abs(cActor.Position[2] - player.Position[2]) >= kCapsuleTop || (cPasses(cId) && cPasses(kPlayer)))
            continue;
        glm::vec2 off(player.Position[0] - cActor.Position[0], player.Position[1] - cActor.Position[1]);
        if (glm::length(off) >= 2 * kCapsuleRadius)
            continue;
        if (glm::length(off) < 1e-3f)
            off = {1.f, 0.f};
        off = glm::normalize(off) * (2 * kCapsuleRadius);
        player.Position[0] = cActor.Position[0] + off.x;
        player.Position[1] = cActor.Position[1] + off.y;
        ++PlayerShoves;
    }
}

void FakeEngine::GraphEntry(Actor& aActor, std::string aInput) const
{
    const bool cOwn = aActor.Copy && !m_replaying && (aInput.starts_with("update") || aInput.starts_with("event"));
    const auto& cGraph = aActor.Graph;
    const auto cState = cGraph.BlendState == aActor.GraphState && !cGraph.Child.empty() ? aActor.GraphState + "/" + cGraph.Child : aActor.GraphState;
    aActor.Graph.History.push_back({aActor.Graph.Timeline, cState, cOwn ? "own " + aInput : std::move(aInput)});
}

std::vector<uint32_t> FakeEngine::GraphWords(const Actor& acActor)
{
    std::vector<uint32_t> words(2 * kGraphWords, 0);
    const auto& cVariables = acActor.GraphVariables;
    for (size_t i = 0; i < kGraphWords && i < cVariables.Integers.size(); ++i)
        words[i] = cVariables.Integers[i];
    for (size_t i = 0; i < kGraphWords && i < cVariables.Floats.size(); ++i)
        words[kGraphWords + i] = std::bit_cast<uint32_t>(cVariables.Floats[i]);
    return words;
}

void FakeEngine::SetGraphWords(Actor& aActor, std::span<const uint32_t> aWords)
{
    auto& variables = aActor.GraphVariables;
    variables.Integers.resize(std::max<size_t>(variables.Integers.size(), kGraphWords));
    variables.Floats.resize(std::max<size_t>(variables.Floats.size(), kGraphWords));
    for (size_t i = 0; i < kGraphWords && i < aWords.size(); ++i)
        variables.Integers[i] = aWords[i];
    for (size_t i = 0; i < kGraphWords && kGraphWords + i < aWords.size(); ++i)
        variables.Floats[i] = std::bit_cast<float>(aWords[kGraphWords + i]);
}

// The graph's event table: every event the rules name, sorted, the same on every game (as a behaviour file's).
std::vector<std::string> FakeEngine::GraphEventTable() const
{
    std::set<std::string> names;
    for (const auto& [cKey, cTo] : GraphTransitions)
        names.insert(cKey.second);
    for (const auto& [cEvent, cWrite] : ActionWrites)
        names.insert(cEvent);
    if (OwnerClipEnds)
        names.insert(OwnerClipEnds->Events.begin(), OwnerClipEnds->Events.end());
    return {names.begin(), names.end()};
}

uint16_t FakeEngine::GraphEventId(const std::string& acEvent) const
{
    const auto cEvents = GraphEventTable();
    const auto it = std::ranges::find(cEvents, acEvent);
    return it == cEvents.end() ? UINT16_MAX : uint16_t(it - cEvents.begin());
}

// A clip whose files its process does not hold lasts no time (59263): its graph runs on to the next.
void FakeEngine::EnterClip(Actor& aActor) const
{
    for (const auto& [cEvent, cState] : ClipFiles)
        if (cState == aActor.GraphState && !aActor.LoadedClips.contains(cEvent))
            if (const auto cNext = SkippedClips.find(aActor.GraphState); cNext != SkippedClips.end())
            {
                aActor.GraphState = cNext->second;
                return;
            }
}

bool FakeEngine::TakeGraphEvent(Actor& aActor, const std::string& acEvent)
{
    if (const auto* pBlend = Graph ? Graph->BlendOf(aActor.GraphState) : nullptr; pBlend && !pBlend->FreezeOn.empty() && pBlend->FreezeOn == acEvent)
    {
        const auto& cIntegers = aActor.GraphVariables.Integers;
        aActor.Graph.FrozenParameter = pBlend->ChooseFrom < cIntegers.size() ? cIntegers[pBlend->ChooseFrom] : 0;
        GraphEntry(aActor, "event " + acEvent);
        return true;
    }
    else if (pBlend && !pBlend->CrossBlendOn.empty() && pBlend->CrossBlendOn == acEvent && aActor.Graph.FrozenParameter)
    {
        aActor.Graph.FrozenParameter.reset();
        aActor.Graph.CrossBlendLeftMs = pBlend->CrossBlendMs;
        GraphEntry(aActor, "event " + acEvent);
        return true;
    }
    const auto it = GraphTransitions.find({aActor.GraphState, acEvent});
    if (it == GraphTransitions.end())
        return false;
    aActor.Graph.EventWords = GraphWords(aActor);
    aActor.GraphState = it->second;
    if (const auto cPicks = GraphPicks.find(aActor.GraphState); cPicks != GraphPicks.end())
        aActor.GraphState = cPicks->second[(DrawGraphRandom(aActor) >> 13) % cPicks->second.size()];
    EnterClip(aActor);
    if (Graph)
        GraphEntry(aActor, "event " + acEvent);
    return true;
}

// Channels (63590): this game's actor's into its graph's variables.
void FakeEngine::WriteGraphChannels(Actor& aActor, double aStepMs)
{
    auto& graph = aActor.Graph;
    if (const auto cTurnDelta = Graph->TurnDeltaChannel)
    {
        auto& floats = aActor.GraphVariables.Floats;
        floats.resize(std::max(floats.size(), *cTurnDelta + 1));
        const float cHeading = aActor.Rotation[2];
        floats[*cTurnDelta] = graph.HeadingKnown ? std::remainder(cHeading - graph.LastHeading, 6.28318531f) / float(aStepMs / 1000.0) : 0.f;
        graph.LastHeading = cHeading;
        graph.HeadingKnown = true;
    }
}

// The behaviour graph's update with this step (63587): its rules on its variables, on its own time (Timeline).
// A looping clip's update; an event every N loops is the graph's, no action behind it (BSEventEveryNEventsModifier).
void FakeEngine::StepLoop(Actor& aActor)
{
    if (aActor.GraphState != Loop->State && aActor.GraphState != Loop->Other)
        return;
    if (++aActor.LoopFrames < Loop->Frames)
        return;
    aActor.LoopFrames = 0;
    if (++aActor.LoopsSeen < aActor.LoopsToSend)
        return;
    aActor.LoopsSeen = 0;
    aActor.LoopsToSend = 1 + (DrawGraphRandom(aActor) >> 13) % Loop->Most;
    aActor.LoopDraws.push_back(aActor.LoopsToSend);
    aActor.GraphState = aActor.GraphState == Loop->State ? Loop->Other : Loop->State;
}

void FakeEngine::StepGraph(Actor& aActor, double aStepMs)
{
    if (Loop)
        StepLoop(aActor);
    auto& graph = aActor.Graph;
    graph.Timeline += aStepMs;
    graph.CrossBlendLeftMs = std::max(0.0, graph.CrossBlendLeftMs - aStepMs);
    graph.Updated = true;
    graph.LastStepMs = aStepMs;
    auto& floats = aActor.GraphVariables.Floats;
    if (const auto& cDamp = Graph->Damping)
    {
        floats.resize(std::max(floats.size(), std::max(cDamp->Input, cDamp->Output) + 1));
        floats[cDamp->Output] = graph.Damped; // its output binding, as the update copies members out
    }
    for (const auto& cRange : Graph->Ranges)
        if (aActor.GraphState == cRange.State && cRange.Input < floats.size())
            if (const auto& cNext = floats[cRange.Input] >= cRange.Bound ? cRange.Above : cRange.Below; !cNext.empty())
                aActor.GraphState = cNext;
    if (const auto* pBlend = Graph->BlendOf(aActor.GraphState))
    {
        // The active-node walk (60079), only while the flag is set: a generate pass's new child.
        if (graph.BlendState == aActor.GraphState && pBlend->Generated && std::exchange(graph.ActiveNodesChanged, false) &&
            !graph.PendingChild.empty())
            graph.Child = graph.PendingChild;
        if (graph.BlendState != aActor.GraphState && !pBlend->Children.empty())
        {
            auto& integers = aActor.GraphVariables.Integers;
            const size_t cPick = pBlend->ChooseFrom < integers.size() ? integers[pBlend->ChooseFrom] : 0;
            graph.BlendState = aActor.GraphState;
            graph.Child = graph.PendingChild = pBlend->Children[std::min(cPick, pBlend->Children.size() - 1)];
        }
    }
    else if (!Graph->MatchNode(aActor.GraphState))
    {
        graph.BlendState.clear();
        graph.Child.clear();
        graph.PendingChild.clear();
        graph.FrozenParameter.reset();
        graph.CrossBlendLeftMs = 0.0;
    }
    graph.Picked.reset();
    if (const auto cNode = Graph->MatchNode(aActor.GraphState))
    {
        // Its match (59583): its own ragdoll's pose; a copy on its stream takes its owner's and reads nothing.
        const auto& cChildren = Graph->Matches[*cNode - 4].Children;
        // Activating, the node forgets its match (duo: a copy's "had -1" at each knockdown); an owner's matches at once.
        if (graph.BlendState != aActor.GraphState)
            graph.Matches.erase(*cNode);
        if (!m_replaying)
            graph.Matches[*cNode] = int32_t(aActor.LyingPose);
        else if (const auto it = std::ranges::find(graph.OwnersPicks, *cNode, &GraphPick::Node); it != graph.OwnersPicks.end())
            graph.Matches[*cNode] = it->Value;
        graph.BlendState = aActor.GraphState;
        const auto cMatch = graph.Matches.find(*cNode);
        // No match yet: no child gets any weight.
        graph.Child = graph.PendingChild =
            cMatch == graph.Matches.end() || cChildren.empty() ? std::string{} : cChildren[std::min<size_t>(cMatch->second, cChildren.size() - 1)];
        if (!m_replaying)
            graph.Picked = GraphPick{*cNode, cMatch->second}; // each update it matches in (59583's hook)
    }
    const std::vector<GraphPick> cOwnersPicks = std::exchange(graph.OwnersPicks, {});
    const auto* pClip = Graph->ClipOf(aActor.GraphState);
    if (!pClip)
    {
        graph.ClipStartedAt.reset();
        graph.Stuck = graph.EndsSent = false;
        GraphEntry(aActor, std::format("update {:.1f}", aStepMs));
        return;
    }
    if (const auto cNode = Graph->SceneNode(aActor.GraphState))
    {
        // Its scene's answer (63050): its instance's once every actor reached its mark, a default scene's at once; a
        // copy on its stream takes its owner's, kept from update to update.
        if (!m_replaying)
        {
            if (aActor.InPair && aActor.MarkWaitMs > 0.0)
                aActor.MarkWaitMs -= aStepMs;
            graph.Matches[*cNode] = !(aActor.InPair && aActor.MarkWaitMs > 0.0);
            graph.Picked = GraphPick{*cNode, graph.Matches[*cNode]};
        }
        else if (const auto it = std::ranges::find(cOwnersPicks, *cNode, &GraphPick::Node); it != cOwnersPicks.end())
            graph.Matches[*cNode] = it->Value;
        if (const auto cAt = graph.Matches.find(*cNode); cAt != graph.Matches.end() && !cAt->second)
        {
            // Not at the mark: its clip waits.
            if (graph.ClipStartedAt)
                *graph.ClipStartedAt += aStepMs;
            GraphEntry(aActor, std::format("update {:.1f}", aStepMs));
            return;
        }
    }
    auto& integers = aActor.GraphVariables.Integers;
    integers.resize(std::max(integers.size(), std::max(pClip->StartFrom.value_or(0), pClip->ExitVariable.value_or(0)) + 1));
    if (!graph.ClipStartedAt)
    {
        // Its event came before this update, which played the clip's first step (20123).
        graph.ClipStartedAt = graph.Timeline - aStepMs;
        graph.ReadAtStart = pClip->StartFrom ? int32_t(integers[*pClip->StartFrom]) : -1;
    }
    if (!graph.Stuck && graph.Timeline >= *graph.ClipStartedAt + pClip->Ms)
    {
        graph.ClipStartedAt.reset();
        graph.EndsSent = false;
        graph.ReadAtEnd = pClip->StartFrom ? int32_t(integers[*pClip->StartFrom]) : -1;
        if (pClip->ExitVariable && !std::ranges::contains(pClip->ExitValues, int32_t(integers[*pClip->ExitVariable])))
            graph.Stuck = true;
        else
        {
            aActor.GraphState = pClip->Starts.at(pClip->StartFrom ? std::min<size_t>(integers[*pClip->StartFrom], pClip->Starts.size() - 1) : 0);
            EnterClip(aActor);
        }
    }
    GraphEntry(aActor, std::format("update {:.1f}", aStepMs));
}

void FakeEngine::GenerateGraph(Actor& aActor) const
{
    auto& graph = aActor.Graph;
    if (const auto* pBlend = Graph->BlendOf(aActor.GraphState); pBlend && pBlend->Generated && graph.BlendState == aActor.GraphState)
    {
        const auto& cIntegers = aActor.GraphVariables.Integers;
        const size_t cPick = graph.FrozenParameter ? *graph.FrozenParameter : pBlend->ChooseFrom < cIntegers.size() ? cIntegers[pBlend->ChooseFrom] : 0;
        if (const auto& cChild = pBlend->Children[std::min(cPick, pBlend->Children.size() - 1)]; cChild != graph.Child)
        {
            graph.PendingChild = cChild;
            graph.ActiveNodesChanged = true;
        }
    }
    const auto& cDamp = Graph->Damping;
    if (!cDamp)
        return;
    auto& floats = aActor.GraphVariables.Floats;
    floats.resize(std::max(floats.size(), std::max(cDamp->Input, cDamp->Output) + 1));
    graph.Damped += (floats[cDamp->Input] - graph.Damped) * std::min(1.f, cDamp->Gain * float(graph.LastStepMs / 1000.0));
    floats[cDamp->Output] = graph.Damped;
}

// One graph update (63358): channels, then the step; an owner's recorded for its copies (Games/Animation's hooks).
void FakeEngine::UpdateGraph(uint32_t aFormId, Actor& aActor)
{
    const auto cStep = float(aActor.AnimationStepMs); // the stream carries it as a float, as Skyrim's deltaTime
    WriteGraphChannels(aActor, cStep);
    auto& streams = World::Get().GetGraphStreams();
    // In a pair or a kill move it is recorded on, but gives no sync point (Games/Skyrim's 63587 hook).
    const bool cOwnBody = aActor.InPair || aActor.InKillMove || aActor.InPairClip;
    const bool cRecorded = !aActor.Copy && streams.Recording(aFormId);
    if (cRecorded)
        streams.HoldPointsWhile(aFormId, cOwnBody);
    const GraphSeed cSeed{aActor.GraphRandom, 0};
    if (cRecorded && streams.PointWanted(aFormId, GraphIdentity(aActor)))
    {
        // As the update begins: its whole graph (Havok's internal state, 58372) and the clip sets its process holds.
        std::vector<std::pair<uint8_t, uint16_t>> sets;
        if (!aActor.LoadedClips.empty())
            if (const uint16_t cClips = GraphEventId(*aActor.LoadedClips.begin()); cClips != UINT16_MAX)
                sets.emplace_back(3, cClips);
        streams.AddPoint(aFormId, {.Tick = streams.Tick(), .State = SaveGraphInternalState(aFormId), .Words = GraphWords(aActor),
                                   .Seed = {aActor.GraphRandom, 0}, .ActiveNodesChanged = aActor.Graph.ActiveNodesChanged,
                                   .Project = aActor.GraphProject,
                                   .ClipSets = std::move(sets)});
    }
    if (cRecorded)
        streams.OnUpdate(aFormId, streams.Tick(), cStep, GraphWords(aActor), cSeed, GraphIdentity(aActor));
    StepGraph(aActor, cStep);
    // 59583's hook: its node's pick from outside its graph, with this update.
    if (cRecorded && aActor.Graph.Picked)
        streams.OnPick(aFormId, aActor.Graph.Picked->Node, aActor.Graph.Picked->Value);
    if (cRecorded)
        streams.OnUpdated(aFormId, GraphWords(aActor));
}

namespace
{
// The replayer's surface over the fake's graph.
struct FakeGraphTarget final : IGraphTarget
{
    FakeEngine& Engine;
    uint32_t FormId;
    FakeEngine::Actor& Actor;
    FakeGraphTarget(FakeEngine& aEngine, uint32_t aFormId, FakeEngine::Actor& aActor) : Engine(aEngine), FormId(aFormId), Actor(aActor) {}

    std::vector<uint32_t> Words() override { return FakeEngine::GraphWords(Actor); }
    void WriteWords(std::span<const uint32_t> aWords) override { FakeEngine::SetGraphWords(Actor, aWords); }
    void SetSeed(const GraphSeed& acSeed) override { Actor.GraphRandom = acSeed.Seed; }
    bool LoadPoint(const Payloads::GraphSyncPoint& acPoint) override
    {
        if (acPoint.Project != Actor.GraphProject)
            ++Actor.ForeignPoints;
        if (!Engine.LoadGraphInternalState(FormId, acPoint.State))
            return false;
        Actor.Graph.ActiveNodesChanged = acPoint.ActiveNodesChanged; // the restore's own walk cleared it
        Actor.GraphRandom = acPoint.Seed.Seed;
        if (!Actor.Graph.History.empty())
            Actor.Graph.History.back().Input = "sync point";
        return true;
    }
    void SendEvent(uint16_t aEventId) override
    {
        if (const auto cEvents = Engine.GraphEventTable(); aEventId < cEvents.size())
            Engine.TakeGraphEvent(Actor, cEvents[aEventId]);
    }
    void Picks(std::span<const GraphPick> aPicks) override { Actor.Graph.OwnersPicks.assign(aPicks.begin(), aPicks.end()); }
    void Update(float aStep) override { Engine.StepGraph(Actor, aStep); }
    void Generate() override { Engine.GenerateGraph(Actor); }
    std::shared_ptr<std::atomic<bool>> RequestClips(uint8_t, uint16_t aEventId) override
    {
        const auto cEvents = Engine.GraphEventTable();
        return aEventId < cEvents.size() ? Engine.RequestActionClips(FormId, cEvents[aEventId]) : nullptr;
    }
};
} // namespace

void FakeEngine::ReplayStream(uint32_t aFormId, Actor& aActor)
{
    FakeGraphTarget target(*this, aFormId, aActor);
    m_replaying = true;
    World::Get().GetGraphStreams().Replay(aFormId, target, GraphIdentity(aActor), aActor.GraphProject);
    m_replaying = false;
    aActor.Graph.Updated = false; // its generate passes are its owner's, in the replay (Games/Skyrim's 63587 hook)
}

void FakeEngine::ReloadGraph(uint32_t aFormId, const std::string& aState, uint32_t aProject)
{
    auto& actor = Actors.at(aFormId);
    ++actor.GraphGeneration;
    if (aProject)
        actor.GraphProject = aProject;
    actor.GraphState = aState;
    actor.GraphVariables = {};
    actor.Graph.ClipStartedAt.reset();
    actor.Graph.Stuck = false;
    actor.Graph.Matches.clear();
    GraphEntry(actor, "reload");
}

void FakeEngine::Frame()
{
    // A pair's clip runs on its own: its blow lands at HitFrame, a transition can hold off pairedStop until then.
    for (auto& [cId, actor] : Actors)
        if (actor.PairBlowIn && --actor.PairBlowIn == 0 && Actors.contains(actor.PairVictim))
            Strikes(cId, actor.PairVictim, 1000.f);
    // Without the graph's rules, clips loop on each game's own clock; with them, in the graph's step (StepGraph).
    if (Loop && !Graph)
        for (auto& [cId, actor] : Actors)
            StepLoop(actor);
    const uint64_t cFrameTick = World::Get().GetFrameTick();
    const double cFrameMs = FrameMs > 0.0 ? FrameMs : LastFrameTick ? double(cFrameTick - LastFrameTick) : 0.0;
    LastFrameTick = cFrameTick;
    for (auto& [cId, actor] : Actors)
    {
        actor.AnimationStepMs = 0.0;
        if (!actor.GraphUpdating)
            continue;
        // A recorded owner's is not interpolated (the 63587 hook's interpAllowed), so it steps every frame.
        if (FarOwnerChunkMs <= 0.0 || actor.Copy || World::Get().GetGraphStreams().Recording(cId))
        {
            actor.AnimationStepMs = cFrameMs;
            actor.InterpCountdownMs = 0.0;
        }
        else if ((actor.InterpCountdownMs -= cFrameMs) <= 0.0)
        {
            actor.AnimationStepMs = FarOwnerChunkMs;
            actor.InterpCountdownMs += FarOwnerChunkMs;
        }
        if (actor.AnimationStepMs <= 0.0)
            actor.Graph.Updated = false; // no step this frame: no generate pass either
        actor.AnimationMs += actor.AnimationStepMs;
    }
    // An owner's AI acts on its clip's time, in frames its graph does not update too (a chunked one's end lands on its
    // next chunk).
    if (Graph && OwnerClipEnds)
        for (auto& [cId, actor] : Actors)
        {
            auto& graph = actor.Graph;
            const auto* pClip = Graph->ClipOf(actor.GraphState);
            if (actor.Copy || graph.EndsSent || !graph.ClipStartedAt || !pClip || actor.GraphState != OwnerClipEnds->State)
                continue;
            const double cEnd = *graph.ClipStartedAt + pClip->Ms;
            const bool cChunked = FarOwnerChunkMs > 0.0;
            const double cUntilEnd = cChunked ? (graph.Timeline + FarOwnerChunkMs >= cEnd ? actor.InterpCountdownMs : 1e9)
                                              : cEnd - graph.Timeline;
            if (cUntilEnd > OwnerClipEnds->LeadMs)
                continue;
            graph.EndsSent = true;
            for (const auto& cEvent : OwnerClipEnds->Events)
            {
                ActionEvent end;
                end.ActionId = 0x13000 + uint32_t(cEvent.size());
                end.EventName = cEvent;
                PerformAnimation(cId, end);
            }
        }
    if (Graph)
        for (auto& [cId, actor] : Actors)
        {
            // On its own body (in a pair): off its owner's stream, and back after (Games/Skyrim's 63587 hook).
            if (auto& streams = World::Get().GetGraphStreams(); actor.Copy && streams.Held(cId))
                streams.HoldWhile(cId, ::ReplayState::CopyOwnBody(actor.InPair, actor.InKillMove, false, SitState(cId)));
            if (actor.Copy)
                World::Get().GetGraphStreams().SeeGraph(cId, GraphIdentity(actor));
            // A copy on the stream: its owner's inputs alone (graph-input-stream.md).
            if (actor.Copy && World::Get().GetGraphStreams().OnStream(cId))
            {
                actor.ExitIn = 0; // its sync point replaced its own clip: no exit event
                if (actor.GraphUpdating)
                    ReplayStream(cId, actor);
            }
            else
            {
                if (actor.AnimationStepMs > 0.0)
                    UpdateGraph(cId, actor);
                // Its exit clip's own event (42835): standing, off its mount.
                if (actor.ExitIn && !--actor.ExitIn)
                {
                    actor.State1 &= ~(0xFu << kSitShift);
                    Mounts.erase(cId);
                }
            }
        }
    // Each graph's generate pass after every graph's update (63358 -> 63589 -> 63568), when it updated this frame.
    if (Graph)
        for (auto& [cId, actor] : Actors)
        {
            if (!actor.GraphUpdating || !actor.Graph.Updated)
                continue;
            if (auto& streams = World::Get().GetGraphStreams(); !actor.Copy && streams.Recording(cId))
                streams.OnGenerate(cId, streams.Tick());
            GenerateGraph(actor);
        }
    for (auto it = ClipLoads.begin(); it != ClipLoads.end();)
    {
        if (it->Frames-- > 0)
        {
            ++it;
            continue;
        }
        if (const auto cActor = Actors.find(it->FormId); cActor != Actors.end())
            cActor->second.LoadedClips = {it->Event};
        it->Done->store(true);
        it = ClipLoads.erase(it);
    }
    if (Actors.contains(kPlayer))
        ShovePlayer();
    // Load jobs end (QueuedReference::Finish): a model attaches; then Actor::FinishLoad3D (HookFinishLoad3D).
    if (!LoadsHeld)
        for (auto& [cId, actor] : Actors)
        {
            if (!std::exchange(actor.LoadQueued, false))
                continue;
            if (std::exchange(actor.ModelReady, false))
            {
                actor.Loaded = true;
                World::Get().GetRunner().Trigger(ReferenceLoadedEvent{cId});
            }
            if (!actor.Refused3D && !(ActorHas3D(cId) && HeldOnGuest(cId)))
                continue;
            if (actor.Refused3D)
                World::Get().GetRunner().Trigger(NpcHeldEvent{cId});
            World::Get().GetRunner().Queue([this, cId] {
                if (Actors.contains(cId))
                    ApplyHeld3D(cId);
            });
        }
    BuildHeads();
    // Getting up (knock states 5 and 6): its ragdoll's bodies stay simulated, driven toward its animation's pose
    // (58291); one this game's world holds stays where it is. Standing, they leave the world.
    for (auto& [cId, actor] : Actors)
    {
        if (!actor.Rising)
            continue;
        const bool cStands = --actor.Rising == 0;
        for (auto& [cKey, body] : Bodies)
        {
            if (body.RagdollOf != cId)
                continue;
            body.Flight.Body.LinearVelocity = {};
            body.Flight.Body.AngularVelocity = {};
            body.Awake = !cStands;
            body.OutOfWorld = cStands;
            body.Held = false;
            if (cStands || body.Caught)
                continue;
            const glm::vec3 cTo = StandingPose(cId, cKey.Index) - body.Flight.Body.Position;
            const float cStep = kRisingSpeed / 60.f;
            const glm::vec3 cMove = glm::length(cTo) > cStep ? glm::normalize(cTo) * cStep : cTo;
            body.Flight.Body.Position += cMove;
            body.Flight.Body.LinearVelocity = cMove * 60.f;
        }
    }
    for (auto& [cId, actor] : Actors)
    {
        if (!actor.Copy)
            continue;
        if (actor.KnockIn && !--actor.KnockIn)
            actor.KnockedDown = true;
        for (const auto& cDrive : m_pendingRagdolls.Take(cId))
        {
            // After its own physics (CopyRagdollPhysics steers after 58291): lying or getting up, never standing.
            if (!actor.Dead && !KnockReplay::BodiesSimulated(KnockStateOf(cId)))
                continue;
            const auto it = Bodies.find(cDrive.Frame.Key);
            if (it == Bodies.end())
                continue;
            auto& body = it->second;
            // At its owner's rest it is left alone (CopyRagdollPhysics): steered on, Havok never lets it sleep.
            if (cDrive.Rest)
            {
                if (body.RestAt != cDrive.Frame.Position)
                {
                    body.RestAt = cDrive.Frame.Position;
                    body.RestSteps = 0;
                }
                // Steered a while, put exactly there, then left alone (CopyRagdollPhysics).
                const auto cStep = BodyPrediction::StepToRest(body.RestSteps);
                if (body.RestSteps <= BodyPrediction::kRestSettleSteps)
                    ++body.RestSteps;
                if (cStep == BodyPrediction::RestStep::kLeave)
                    continue;
            }
            else
                body.RestAt.reset();
            body.Held = true;
            body.Awake = false;
            body.RagdollOf = cId;
            body.Flight.Body.Position = cDrive.Frame.Position;
            body.Flight.Body.Rotation = cDrive.Frame.Rotation;
            ++body.Drives;
        }
        // Drawn: moved since its graph update while its ragdoll poses it, its old pose shows at the new place.
        const float cTurn = std::abs(std::remainder(actor.Rotation[2] - actor.GraphPlace[3], 6.28318531f));
        const float cSnap = glm::distance(glm::vec3(actor.Position[0], actor.Position[1], actor.Position[2]),
                                          glm::vec3(actor.GraphPlace[0], actor.GraphPlace[1], actor.GraphPlace[2])) + cTurn * kSnapReach;
        if (cSnap > 0.5f && RagdollPosed(cId))
        {
            ++actor.Snaps;
            actor.WorstSnap = std::max(actor.WorstSnap, cSnap);
        }
        // Its graph update: lying in its ragdoll, the engine keeps it on its root body, turned with it (37996).
        if (RagdollPosed(cId))
            for (const auto& [cKey, cBody] : Bodies)
                if (cBody.RagdollOf == cId && cKey.Index == 0)
                {
                    std::copy_n(&cBody.Flight.Body.Position.x, 3, actor.Position);
                    actor.Rotation[2] = HeadingOf(cBody.Flight.Body.Rotation);
                }
        std::copy_n(actor.Position, 3, actor.GraphPlace);
        actor.GraphPlace[3] = actor.Rotation[2];
    }
    ShakeSeconds = std::max(0.f, ShakeSeconds - 1.f / 60);
    // Game time at the time scale for the real time since the last frame, as the engine's frame delta is.
    const auto cNow = std::chrono::steady_clock::now();
    if (m_lastFrame.time_since_epoch().count())
        Calendar.Time += Calendar.TimeScale * std::chrono::duration<float>(cNow - m_lastFrame).count() / 3600.f;
    m_lastFrame = cNow;
    if (Calendar.Time >= 24.f)
    {
        Calendar.Time -= 24.f;
        ++Calendar.Day;
        ++Calendar.DaysPassed;
    }
    if (auto& player = Actors.at(kPlayer); player.Dead)
    {
        if (player.DyingFrames && !--player.DyingFrames)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{kPlayer});
        if (++player.DeadFrames >= kDeathReloadFrames && !DeathReloadHeld)
            Reloaded = true;
    }
    ++DetectionFrame;
    StealthEye();
    ProcessTracking();
    DetectionPass();
    RunQueued();
}

void FakeEngine::Queue(LocalCall aCall, uint32_t aCause)
{
    m_queued.emplace_back(std::move(aCall), aCause);
}

void FakeEngine::RunQueued()
{
    auto queued = std::exchange(m_queued, {});
    for (const auto& [cCall, cCause] : queued)
    {
        const auto* cpSelf = std::get_if<LocalForm>(&cCall.Self);
        if (cpSelf && QuestFunctions.contains({cpSelf->FormId, cCall.Function}))
            CallsQuest(cCall, cCause, false, false);
        else
            RunScript(cCall, cCause);
    }
}

void FakeEngine::CallsQuest(const LocalCall& acCall, uint32_t aCause, bool aStory, bool aFromBody)
{
    const auto* cpQuest = std::get_if<LocalForm>(&acCall.Self);
    const uint32_t cQuest = cpQuest ? cpQuest->FormId : 0;
    const auto it = QuestFunctions.find({cQuest, acCall.Function});
    if (it == QuestFunctions.end())
        return;
    const bool cOwnFunction = IsQuestScriptFunction(acCall.Class); // not Quest.SetStage's wrapper
    if (cOwnFunction && LearnsSystemQuest(cQuest, 0, aFromBody, m_inSystemQuest, false))
        SystemQuests::Learn(cQuest, true);
    if (!cOwnFunction || !SystemQuestList.Contains(cQuest))
    {
        it->second(*this, acCall, aCause);
        return;
    }
    const auto cIsGuestCharacter = [this](uint32_t aActor) {
        const auto cIt = Actors.find(aActor);
        return !IsGuest && cIt != Actors.end() && cIt->second.Copy && cIt->second.Player;
    };
    const Routing cRouting = InSession ? RouteOfSystemCall(aCause, aStory, cIsGuestCharacter(aCause), !IsGuest, false) : Routing{};
    if (cRouting.To == Destination::kCauseGame)
    {
        World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kOwner, .GuestFormId = aCause, .Call = acCall});
        return;
    }
    if (cRouting.To == Destination::kEveryGame)
    {
        LocalCall call = acCall;
        MarkEachPlayer(call, cRouting.MarkFor);
        if (cRouting.ThisPlayer)
            PartyNatives::Run(OnThisPlayer(call), 0);
        World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kOthers, .Call = std::move(call)});
        if (!cRouting.Original)
            return;
    }
    if (InSession)
        World::Get().GetRunner().Trigger(PlayerProgressEvent{});
    const bool cOuter = std::exchange(m_inSystemQuest, true);
    it->second(*this, acCall, aCause);
    m_inSystemQuest = cOuter;
}

void FakeEngine::FiresQuestTimer(uint32_t aQuest, const std::string& acFunction)
{
    // Its own event: this game's player's (WorldEvents' gate delivers it as this player's).
    CallsQuest({.Class = "Quest", .Function = acFunction, .Self = LocalForm{aQuest}}, kPlayer);
}

std::string FakeEngine::ReadSystemQuests() const
{
    SystemQuestState state;
    for (const uint32_t cQuest : SystemQuestList.All())
    {
        SystemQuestState::Quest& quest = state.Quests.emplace_back(SystemQuestState::Quest{GameId(0, cQuest)});
        if (const auto cIt = QuestVariables.find(cQuest); cIt != QuestVariables.end())
            for (const auto& [cName, cValue] : cIt->second)
                quest.Variables.push_back({"Script", cName, Utils::ToScriptValue(cValue)});
        if (const auto cIt = QuestTimers.find(cQuest); cIt != QuestTimers.end())
            for (const auto& [cEvent, cTimer] : cIt->second)
                quest.Timers.push_back(cTimer);
    }
    for (const uint32_t cGlobal : SystemQuestList.Globals())
        state.Globals.push_back({GameId(0, cGlobal), Globals.contains(cGlobal) ? Globals.at(cGlobal) : 0.f});
    return state.Encode();
}

void FakeEngine::ApplySystemQuests(std::string_view aState)
{
    const auto cState = SystemQuestState::Decode(aState);
    for (const uint32_t cQuest : SystemQuestList.All())
    {
        // As SystemQuests::ApplyState: the save's instance (the host's player's) goes; this player's comes back, or
        // its scripts start over.
        QuestVariables.erase(cQuest);
        QuestTimers.erase(cQuest);
        const SystemQuestState::Quest* cpKept = cState ? cState->Find(GameId(0, cQuest)) : nullptr;
        if (!cpKept)
            continue;
        for (const SystemQuestState::Variable& cVariable : cpKept->Variables)
            if (const auto cValue = Utils::ToLocalValue(cVariable.Value))
                QuestVariables[cQuest][cVariable.Name] = *cValue;
        for (const SystemQuestState::Timer& cTimer : cpKept->Timers)
            QuestTimers[cQuest][cTimer.Native] = cTimer;
    }
    if (cState)
        for (const SystemQuestState::Global& cGlobal : cState->Globals)
            if (SystemQuestList.IsGlobal(cGlobal.Id.BaseId))
                Globals[cGlobal.Id.BaseId] = cGlobal.Value;
}

FakeEngine& FakeEngine::Current() noexcept
{
    return World::Get().ctx().at<FakeEngine&>();
}

GameId SessionIdOf(uint32_t aFormId) noexcept
{
    return GameId(aFormId >> 24, aFormId & 0x00FFFFFF);
}

uint32_t FormIdOf(const GameId& acId) noexcept
{
    return (acId.ModId << 24) | acId.BaseId;
}

// The engine's created potions (ItemIds): the fake's Brewed.
std::optional<Recipe> CreatedItems::RecipeOf(uint32_t aFormId) noexcept
{
    const auto& cBrewed = FakeEngine::Current().Brewed;
    const auto it = cBrewed.find(aFormId);
    return it != cBrewed.end() ? std::optional(it->second) : std::nullopt;
}

uint32_t CreatedItems::Make(const Recipe& acRecipe) noexcept
{
    return FakeEngine::Current().Brew(acRecipe);
}

uint32_t FakeEngine::Brew(const Recipe& acRecipe)
{
    for (const auto& [cId, cRecipe] : Brewed)
        if (cRecipe == acRecipe)
            return cId;
    const uint32_t cId = m_nextTemporary++;
    Brewed.emplace(cId, acRecipe);
    return cId;
}

uint32_t FakeEngine::PlayerBrews(const Recipe& acRecipe, int32_t aCount)
{
    const uint32_t cPotion = Brew(acRecipe);
    GetsItem(kPlayer, cPotion, aCount);
    return cPotion;
}

ActorValues FakeEngine::FullValues()
{
    ActorValues values;
    for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
        values.ActorValuesList[cIndex] = values.ActorMaxValuesList[cIndex] = 100.f;
    return values;
}

// The engine's temporary forms (a reference placed at run time, a copy's NPC): 0xFF ids; a deleted one's id goes to
// the next (docs/re/crashes/2026-10-14-reused-copy-form-id.md).
uint32_t FakeEngine::NewTemporary() noexcept
{
    ++m_made;
    if (m_freedTemporary.empty())
        return m_nextTemporary++;
    return m_freedTemporary.extract(m_freedTemporary.begin()).value();
}

uint32_t FakeEngine::LoadNpc(uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    const uint32_t cId = NewTemporary();
    Actors[cId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z},
                        .Essential = EssentialBases.contains(aBase), .Made = m_made};
    return cId;
}

void FakeEngine::PlaceNpc(uint32_t aFormId, uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    Actors[aFormId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z},
                            .Essential = EssentialBases.contains(aBase)};
}

void FakeEngine::FinishLoading(uint32_t aFormId)
{
    Actors.at(aFormId).Initializing = false;
    Load3D(aFormId);
}

bool FakeEngine::NpcWalks(uint32_t aFormId, uint32_t aCell, const glm::vec3& acPosition)
{
    auto& actor = Actors.at(aFormId);
    if (actor.Copy || HeldOnGuest(aFormId))
        return false;
    const bool cWasIn = Attached.contains(actor.Cell);
    actor.Cell = aCell;
    std::copy_n(&acPosition.x, 3, actor.Position);
    if (!cWasIn && Attached.contains(aCell))
        Load3D(aFormId);
    return true;
}

void FakeEngine::Load3D(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    actor.Refused3D = HeldOnGuest(aFormId);
    // A job ends in Frame (QueuedReference::Finish); a load with no job held back attaches at once, as the services
    // see it. Held back, its model waits for Finish (BackgroundLoad3D's result, +0x2C8).
    actor.LoadQueued = actor.Refused3D || LoadsHeld;
    actor.ModelReady = !actor.Refused3D && LoadsHeld;
    actor.Loaded = !actor.Refused3D && !LoadsHeld;
    if (actor.Loaded)
        World::Get().GetRunner().Trigger(ReferenceLoadedEvent{aFormId});
}

void FakeEngine::ApplyHeld3D(uint32_t aFormId)
{
    const auto& cActor = Actors.at(aFormId);
    // Held with 3D: Reload3D, so refused. Not held after a refused load: queued, unless its job is still out.
    if (HeldOnGuest(aFormId) ? ActorHas3D(aFormId) : cActor.Refused3D && !cActor.LoadQueued)
        Load3D(aFormId);
}

bool FakeEngine::HeldOnGuest(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && GuestNpcs::Held(HoldSession, aFormId == kPlayer, it->second.Copy, it->second.ClaimedIn);
}

void FakeEngine::CheckHealth(uint32_t aFormId, Actor& aActor) noexcept
{
    if (aActor.Copy || aActor.Dead || aActor.Values.ActorValuesList[kHealth] > 0.f)
        return;
    (aActor.Essential ? aActor.Bleeding : aActor.Dead) = true;
    if (aActor.Dead)
    {
        // KillImpl performs the death action (37896): an input of its graph, recorded for its copies, and its graph
        // updates on while it lies dead (Skyrim, duo 2026-11-12: 250 updates in the 250 its living player ran).
        if (auto& streams = World::Get().GetGraphStreams(); Graph && streams.Recording(aFormId) && GraphEventId(kDeathEvent) != UINT16_MAX)
        {
            streams.OnEvent(aFormId, streams.Tick(), GraphEventId(kDeathEvent), GraphWords(aActor), {aActor.GraphRandom, 0});
            TakeGraphEvent(aActor, kDeathEvent);
        }
        else
            aActor.GraphUpdating = false;
        aActor.GraphState = "dead";
        if (aFormId == kPlayer)
        {
            aActor.DyingFrames = kDyingFrames;
            aActor.DeadFrames = 0;
            // KillImpl's death drop (37320): the drawn weapon falls, unless our hook keeps a player's.
            if (aActor.Weapon == WeaponState::Drawn && !DeathDrops::Kept(aActor.Player, World::Get().GetTransport().IsOnline()))
                if (const auto cHeld = std::ranges::find_if(aActor.Items.Entries, [&](const auto& acEntry) {
                        const auto cForm = ItemForms.find(::ItemForm(acEntry.BaseId));
                        return acEntry.ExtraWorn && cForm != ItemForms.end() && cForm->second.Type == kWeapon;
                    });
                    cHeld != aActor.Items.Entries.end())
                {
                    Inventory::Entry dropped = *cHeld;
                    dropped.Count = 1;
                    dropped.ExtraWorn = false;
                    DropsInWorld(dropped);
                }
        }
        auto& action = aActor.LatestAnimation;
        action.Tick = World::Get().GetFrameTick();
        action.ActorId = aFormId;
        action.ActionId = 0x1337;
        action.State1 = DeathActions::kDying;
        action.EventName = kDeathEvent;
        World::Get().GetDispatcher().trigger(action);
        World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId, false});
    }
}

void FakeEngine::Hit(uint32_t aFormId, float aDamage)
{
    DamageActorHealth(aFormId, 0, aDamage);
}

std::vector<uint32_t> FakeEngine::CopiesOf(uint32_t aBase) const
{
    std::vector<uint32_t> copies;
    for (const auto& [cId, cActor] : Actors)
        if (cActor.Copy && (!aBase || cActor.Base == aBase))
            copies.push_back(cId);
    return copies;
}

// The process lists: the actors in a cell loaded here (copies too: they are this game's actors), but the player.
std::vector<uint32_t> FakeEngine::GetLoadedActorFormIds() const noexcept
{
    std::vector<uint32_t> loaded;
    for (const auto& [cId, cActor] : Actors)
        if (cId != kPlayer && ActorHas3D(cId))
            loaded.push_back(cId);
    return loaded;
}

bool FakeEngine::ActorHas3D(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Loaded && !it->second.Disabled && (aFormId == kPlayer || Attached.contains(it->second.Cell));
}

uint32_t FakeEngine::GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept
{
    const auto it = Actors.find(aActorFormId);
    return it != Actors.end() && aSlot < 2 ? it->second.HeldSpells[aSlot] : 0;
}

void FakeEngine::EquipSpells(uint32_t aFormId, const MagicEquipment& acMagic)
{
    auto& actor = Actors.at(aFormId);
    actor.Items.CurrentMagicEquipment = acMagic;
    if (ActorHas3D(aFormId))
    {
        actor.HeldSpells[0] = ::ItemForm(acMagic.LeftHandSpell);
        actor.HeldSpells[1] = ::ItemForm(acMagic.RightHandSpell);
    }
}

void FakeEngine::RevertSelectedSpells(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    actor.Items.CurrentMagicEquipment.LeftHandSpell = actor.Items.CurrentMagicEquipment.RightHandSpell = {};
    actor.HeldSpells[0] = actor.HeldSpells[1] = 0;
}

void FakeEngine::Unload3D(uint32_t aFormId)
{
    Actors.at(aFormId).Loaded = false;
    RevertSelectedSpells(aFormId);
    DropHead(aFormId);
}

void FakeEngine::DropHead(uint32_t aFormId)
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.HeadBuilt = false;
        it->second.HeadTints.reset();
    }
}

void FakeEngine::BuildHeads()
{
    for (auto& [cId, actor] : Actors)
    {
        if (!ActorHas3D(cId))
        {
            actor.HeadBuilt = false;
            actor.HeadTints.reset();
        }
        else if (!actor.HeadBuilt)
        {
            actor.HeadBuilt = true;
            World::Get().GetRunner().Trigger(ActorHeadUpdateEvent{cId});
        }
    }
}

bool FakeEngine::IsActorFacePainted(uint32_t aFormId) const noexcept
{
    return ActorHas3D(aFormId) && Actors.at(aFormId).HeadBuilt && Actors.at(aFormId).HeadTints.has_value();
}

// Only a head that is built takes paint (FaceGenSystem finds no face before).
bool FakeEngine::PaintActorFace(uint32_t aFormId, const Tints& acTints) noexcept
{
    if (!ActorHas3D(aFormId) || !Actors.at(aFormId).HeadBuilt)
        return false;
    Actors.at(aFormId).HeadTints = acTints;
    return true;
}

void FakeEngine::PlayerTravels(uint32_t aCell, const glm::vec3& acPosition)
{
    MoveActorTo(kPlayer, aCell, acPosition.x, acPosition.y, acPosition.z);
}

void FakeEngine::PlayerTravelsUnannounced(uint32_t aCell, const glm::vec3& acPosition)
{
    m_unannouncedMove = true;
    MoveActorTo(kPlayer, aCell, acPosition.x, acPosition.y, acPosition.z);
    m_unannouncedMove = false;
}

void FakeEngine::SetActorCopy(uint32_t aFormId, bool aCopy) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cReleased = it->second.Copy && !aCopy;
        if (aCopy && !it->second.Copy)
            it->second.GraphReady = NewCopiesGraphReady;
        it->second.Copy = aCopy;
        if (!aCopy)
        {
            it->second.OwnerCombatTarget = 0;
            it->second.ClaimedIn = HoldSession;
        }
        ApplyHeld3D(aFormId); // SkyrimBridge::SetActorCopy
        // Skyrim: only Actor::EndInterruptPackage (37474) ends it; the engine then picks the actor's own package.
        if (cReleased && CopyPackage::EndsOnRelease(it->second.RunningPackageType, it->second.DoNothing.packFlags))
            it->second.RunningPackageType = 0;
    }
}

bool FakeEngine::IsActorCopy(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Copy;
}

void FakeEngine::SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Player = aPlayer;
}

uint32_t FakeEngine::GetActorBaseFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Base : 0;
}

bool FakeEngine::SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !aBaseFormId)
        return false;
    it->second.Base = aBaseFormId;
    return true;
}

void FakeEngine::SetActorDisplayName(uint32_t aFormId, const std::string& acName) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.DisplayName = PooledNames.try_emplace(NameKey(acName), acName).first->second;
}

std::string FakeEngine::GetFormName(uint32_t aFormId) const noexcept
{
    if (aFormId == 0x7)
        return PlayerCharacterName();
    const auto it = FormNames.find(aFormId);
    return it != FormNames.end() ? it->second : Name;
}

std::string FakeEngine::GetActorDisplayName(uint32_t aFormId) const noexcept
{
    const auto actor = Actors.find(aFormId);
    if (actor != Actors.end() && !actor->second.DisplayName.empty())
        return actor->second.DisplayName;
    if (aFormId == kPlayer)
        return PlayerCharacterName();
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Name : std::string{};
}

float FakeEngine::GetActorBaseWeight(uint32_t aFormId) const noexcept
{
    if (aFormId == kPlayer)
        return Player.Weight;
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Weight : 50.f;
}

uint32_t FakeEngine::GetActorRaceFormId(uint32_t aFormId) const noexcept
{
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Race : 0;
}

uint32_t FakeEngine::GetActorParentCellFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0;
    // A persistent reference outdoors out of the loaded cells has none (Olava, Skulvar: Skyrim 2026-11-06).
    if (it->second.Persistent && it->second.WorldSpace && !Attached.contains(it->second.Cell))
        return 0;
    return it->second.Cell;
}

uint32_t FakeEngine::GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.WorldSpace : 0;
}

void FakeEngine::GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    const auto it = Actors.find(aFormId);
    const float* cpAt = it != Actors.end() ? it->second.Position : nullptr;
    aX = cpAt ? cpAt[0] : 0.f;
    aY = cpAt ? cpAt[1] : 0.f;
    aZ = cpAt ? cpAt[2] : 0.f;
}

void FakeEngine::GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    const auto it = Actors.find(aFormId);
    const float* cpAt = it != Actors.end() ? it->second.Rotation : nullptr;
    aX = cpAt ? cpAt[0] : 0.f;
    aY = cpAt ? cpAt[1] : 0.f;
    aZ = cpAt ? cpAt[2] : 0.f;
}

bool FakeEngine::IsActorDead(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Dead;
}

void FakeEngine::KillActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cWasDead = it->second.Dead;
        (it->second.Essential ? it->second.Bleeding : it->second.Dead) = true;
        if (it->second.Dead)
        {
            it->second.GraphUpdating = false;
            it->second.GraphState = "dead";
        }
        if (!cWasDead && it->second.Dead)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId});
    }
}

void FakeEngine::FinishDeath(uint32_t aFormId)
{
    Actors.at(aFormId).BlockResurrect = false;
    World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId});
}

// Actor::KillBesideGraph: KillImpl's effects on a copy on its owner's stream, its own death action refused.
void FakeEngine::KillBesideGraph(uint32_t aFormId, const ActionEvent& acAction)
{
    auto& actor = Actors.at(aFormId);
    actor.PoseHeldUntil = 0;
    const bool cWasDead = actor.Dead;
    actor.Dead = true;
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
}

void FakeEngine::ClipKillActor(uint32_t aFormId)
{
    if (auto& actor = Actors.at(aFormId); actor.Copy)
        actor.PoseHeldUntil = World::Get().GetFrameTick() + ::ReplayState::cPoseHoldMs;
}

bool FakeEngine::PoseHeld(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && ::ReplayState::PoseHeld(World::Get().GetFrameTick(), it->second.PoseHeldUntil, it->second.Dead);
}

void FakeEngine::RagdollContact(uint32_t aFormId)
{
    if (const auto& cActor = Actors.at(aFormId); cActor.InKillMove && ::ReplayState::ContactKills(cActor.PairedWithCopy))
        KillActor(aFormId);
}

void FakeEngine::EndKillMove(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    actor.InKillMove = false;
    if (actor.KilledBy)
        KillActor(aFormId);
}

bool FakeEngine::ReplayAnimation(uint32_t aFormId, const ActionEvent& acAction)
{
    auto& actor = Actors.at(aFormId);
    if (actor.Copy)
        actor.ReplayedOn = actor.GraphVariables;
    if (const auto cPartner = Actors.find(acAction.TargetId); !acAction.TargetEventName.empty() && cPartner != Actors.end())
    {
        if (!(actor.InKillMove && cPartner->second.InKillMove) && !::ReplayState::PairMarkClear(!PairMarkBlocked, actor.Copy))
            return false;
        // An uninterruptible transition still playing drops the new pair's event too.
        if ((actor.InPairClip && actor.StopsDropped) || (cPartner->second.InPairClip && cPartner->second.StopsDropped))
            return false;
        // 32803 makes an instance and plays the target's half through its graph (63591); a replay names no target
        // event for a copy partner (PlayReplayedAction): no instance, its half is its owner's action.
        const bool cInstance = !(Replaying && cPartner->second.Copy);
        // A copy on its owner's stream takes the half only held off the stream (Games/Skyrim's 63591 hook); refused,
        // the pair fails.
        if (auto& streams = World::Get().GetGraphStreams();
            cInstance && cPartner->second.Copy && streams.OnStream(acAction.TargetId) && !streams.HoldWhile(acAction.TargetId, true))
            return false;
        // 32803 takes a pair on two actors already paired as the same pair; the second clip ends both at once.
        const bool cCollapsed = actor.InKillMove && cPartner->second.InKillMove;
        CollapsedPairs += cCollapsed;
        // Only a "pa_Kill" action puts both in a kill move (39004); a mount's pair leaves that alone.
        if (cCollapsed || ::ReplayState::EntersKillMove(acAction.EventName))
            actor.InKillMove = cPartner->second.InKillMove = !cCollapsed;
        actor.LatestPairAction = acAction;
        if (PairBlows && !cCollapsed && !actor.Copy && acAction.EventName.starts_with("pa_"))
        {
            actor.PairBlowIn = PairBlowFrames;
            actor.PairVictim = acAction.TargetId;
        }
        actor.InPairClip = cPartner->second.InPairClip = !cCollapsed;
        if (cInstance || cCollapsed)
        {
            actor.InPair = cPartner->second.InPair = !cCollapsed;
            actor.MarkWaitMs = cPartner->second.MarkWaitMs = PairMarkMs; // each walks to its mark first (32792)
        }
        // The victim's half is its own action (TESActionData::Func5 41557 under Animation.cpp's hook): an owner's is
        // sent, and its graph takes the event in the instance.
        if (cInstance && !cCollapsed && !Replaying && !cPartner->second.Copy)
        {
            ActionEvent half = acAction;
            half.ActorId = acAction.TargetId;
            half.TargetId = aFormId;
            half.EventName = acAction.TargetEventName;
            half.TargetEventName = acAction.EventName;
            half.Variables = cPartner->second.GraphVariables;
            half.Variables.GraphRandom = cPartner->second.GraphRandom;
            half.GraphState.clear();
            if (!GraphTransitions.empty() && !cPartner->second.GraphState.empty())
            {
                if (auto& streams = World::Get().GetGraphStreams(); Graph && streams.Recording(half.ActorId) && streams.HoldPointsWhile(half.ActorId, true))
                    streams.OnEvent(half.ActorId, streams.Tick(), GraphEventId(half.EventName), GraphWords(cPartner->second), {cPartner->second.GraphRandom, 0});
                TakeGraphEvent(cPartner->second, half.EventName);
            }
            cPartner->second.LatestAnimation = half;
            World::Get().GetDispatcher().trigger(half);
        }
        actor.PairedWithCopy = cPartner->second.Copy;
        cPartner->second.PairedWithCopy = actor.Copy;
        actor.StopsDropped = cPartner->second.StopsDropped = PairStopsDropped;
    }
    else if (actor.Copy && actor.InKillMove)
        ++BrokenPairs;
    const auto cWeaponBefore = actor.Weapon;
    if (actor.Copy)
    {
        actor.Weapon = WeaponState((ReplayState::Flags2(acAction.State2, uint32_t(actor.Weapon) << 5) >> 5) & 7);
        actor.State1 = ReplayState::Flags1(acAction.State1, actor.State1);
    }
    if (const auto cState = ActionStates.find(acAction.EventName); cState != ActionStates.end())
        actor.State1 = cState->second.second ? actor.State1 | cState->second.first : actor.State1 & ~cState->second.first;
    if (const auto cExit = ExitClips.find(acAction.EventName); cExit != ExitClips.end())
    {
        actor.State1 = (actor.State1 & ~(0xFu << kSitShift)) | uint32_t(::ReplayState::kSitWantsToStand) << kSitShift;
        actor.ExitIn = cExit->second;
    }
    if (!GraphTransitions.empty() && !actor.GraphState.empty())
    {
        // 63591's hook: an owner's event recorded before its graph handles it (Games/Animation).
        if (auto& streams = World::Get().GetGraphStreams(); Graph && !actor.Copy && streams.Recording(aFormId))
            streams.OnEvent(aFormId, streams.Tick(), GraphEventId(acAction.EventName), GraphWords(actor), {actor.GraphRandom, 0});
        // A copy on its stream takes its owner's events alone (63591's hook drops any other).
        const bool cDropped = actor.Copy && World::Get().GetGraphStreams().OnStream(aFormId);
        if (cDropped || !TakeGraphEvent(actor, acAction.EventName))
        {
            ++RefusedAnimations[aFormId];
            if (!ReplayGraph::Recover(false, acAction.GraphState, [&](std::string_view) {
                return LoadActorGraphState(aFormId, acAction.GraphState);
            }))
            {
                actor.Weapon = cWeaponBefore;
                return false;
            }
        }
    }
    return true;
}

uint32_t FakeEngine::DrawGraphRandom(Actor& aActor) noexcept
{
    return aActor.GraphRandom = aActor.GraphRandom * 1664525 + 1013904223;
}

std::shared_ptr<std::atomic<bool>> FakeEngine::RequestActionClips(uint32_t aFormId, const std::string& acEvent)
{
    if (!ClipFiles.contains(acEvent) && !OtherSetClips.contains(acEvent))
        return nullptr;
    auto done = std::make_shared<std::atomic<bool>>(false);
    ClipLoads.push_back({aFormId, acEvent, ClipLoadFrames, done});
    return done;
}

bool FakeEngine::PerformAnimation(uint32_t aFormId, ActionEvent aAction, bool aQuery)
{
    aAction.State2 = (aAction.State2 & ~ReplayState::kWeaponMask) | (uint32_t(Actors.at(aFormId).Weapon) << 5);
    aAction.State1 |= Actors.at(aFormId).State1;
    uint32_t flags = aQuery ? 1 : 0;
    aAction.Type = 2 | (flags ? 4 : 0);
    aAction.Tick = World::Get().GetFrameTick();
    aAction.ActorId = aFormId;
    aAction.Variables = Actors.at(aFormId).GraphVariables; // SaveAnimationVariables, as Animation.cpp's hook
    aAction.Variables.GraphRandom = Actors.at(aFormId).GraphRandom;
    if (ClipFiles.contains(aAction.EventName))
    {
        // Its process loads the action's clip set first (39625), in its stream's place.
        Actors.at(aFormId).LoadedClips = {aAction.EventName};
        if (auto& streams = World::Get().GetGraphStreams(); !Actors.at(aFormId).Copy && streams.Recording(aFormId))
            streams.OnClips(aFormId, streams.Tick(), 3, GraphEventId(aAction.EventName));
    }
    const auto cOutcome = ReplayCapture::Run(flags, [&]() -> uint8_t {
        // PerformAction (38949) resolves a query, changes flag 1 to 2 and never sends its event.
        if (flags & 1)
        {
            flags = 2;
            return 1;
        }
        return ReplayAnimation(aFormId, aAction);
    });
    if (!aQuery)
        ApplyActionWrites(aFormId, aAction.EventName);
    if (cOutcome.Capture && cOutcome.Result)
    {
        aAction.StateAfter1 = Actors.at(aFormId).State1;
        if (::ReplayState::StartsAttack(aAction.EventName))
            Actors.at(aFormId).ClashUntil = 0;
        // Without a behaviour graph no copy is on a stream (a fixture's scripted transitions alone).
        const auto& cActor = Actors.at(aFormId);
        const bool cOwnBody = ::ReplayState::CopyOwnBody(false, false, false, SitState(aFormId));
        if (::ReplayState::CarriesGraphState(!aAction.TargetEventName.empty(), Graph && World::Get().GetGraphStreams().Streaming(aFormId), cOwnBody))
            aAction.GraphState = SaveActorGraphState(aFormId);
        Actors.at(aFormId).LatestAnimation = aAction;
        World::Get().GetDispatcher().trigger(aAction);
    }
    return cOutcome.Result != 0;
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
        actor.State1 = flags[0];
        const uint32_t cFlags2 = actor.Copy ? ReplayState::Flags2(flags[1], uint32_t(actor.Weapon) << 5) : flags[1];
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
    it->second.State1 = ReplayState::Flags1(flags[0], it->second.State1);
    it->second.Weapon = WeaponState((ReplayState::Flags2(flags[1], uint32_t(it->second.Weapon) << 5) >> 5) & 7);
    return true;
}

void FakeEngine::SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.LatestAnimation = acAction;
}

// Actor::Respawn: alive again, values restored; the value writers and Resurrect report it.
void FakeEngine::RespawnActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cWasDead = it->second.Dead;
        const auto cAction = it->second.LatestAnimation;
        it->second.Dead = it->second.Bleeding = false;
        it->second.LatestAnimation = {};
        it->second.GraphState.clear();
        it->second.GraphUpdating = true;
        it->second.Values = FullValues();
        it->second.Bases = FullValues().ActorMaxValuesList;
        for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
            World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
        if (cWasDead)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId, true, true,
                cAction.ActionId && DeathActions::IsDying(cAction.State1) ? std::optional<uint64_t>{cAction.Tick} : std::nullopt});
    }
}

void FakeEngine::ResurrectActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        ++it->second.ResurrectCalls;
        if (it->second.BlockResurrect)
            return;
        bool forced = false;
        DeathActions::WithLocalReset(forced, [&] {
            if (it->second.Dead && !forced)
            {
                ActionEvent reset = it->second.LatestAnimation;
                reset.Tick = World::Get().GetFrameTick();
                reset.ActionId = 0xD1FDE;
                reset.EventName = "IdleForceDefaultState";
                World::Get().GetDispatcher().trigger(reset);
            }
            const auto cGraph = it->second.GraphState;
            const bool cUpdating = it->second.GraphUpdating;
            RespawnActor(aFormId);
            if (it->second.Copy)
            {
                it->second.GraphState = cGraph;
                it->second.GraphUpdating = cUpdating;
            }
        });
    }
}

bool FakeEngine::DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    const bool cWasDead = it->second.Dead;
    it->second.Values.ActorValuesList[kHealth] -= aDamage;
    if (aDamage > 0 && aAttackerFormId)
        World::Get().GetRunner().Trigger(ActorDamagedEvent{aFormId, aAttackerFormId, aDamage});
    World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, kHealth});
    // The engine names the killer before its death event.
    if (!cWasDead && it->second.Values.ActorValuesList[kHealth] <= 0.f)
        it->second.KilledBy = aAttackerFormId;
    CheckHealth(aFormId, it->second);
    return true;
}

bool FakeEngine::IsBleedingOut(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Bleeding;
}

bool FakeEngine::IsOtherPlayer(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Copy && it->second.Player;
}

bool FakeEngine::SetActorPersistent(uint32_t aFormId, bool aPersistent) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        return std::exchange(it->second.Persistent, aPersistent);
    return false;
}

// The player's faction is not modelled.
void FakeEngine::SetActorPlayerFaction(uint32_t) noexcept
{
}

// Stepped or placed, never a copy lying in its ragdoll (SkyrimBridge); graph variables not modelled. A step reaches the
// goal across, never up: Havok's step leaves height to support and gravity (actor-movement.md; flat ground here).
bool FakeEngine::ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3], const float aRotation[3], float,
                                     const AnimationVariables& acVariables, CopyMove aMove) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    // VariableSync::Write writes the variables it is given: none, the graph keeps its own.
    if (!acVariables.Booleans.empty() || !acVariables.Integers.empty() || !acVariables.Floats.empty())
        it->second.GraphVariables = acVariables;
    if (aMove == CopyMove::Leave || RagdollPosed(aFormId))
        return true;
    it->second.Stepped = aMove == CopyMove::Step;
    ++it->second.Movements;
    const float cHeight = it->second.Position[2];
    std::copy_n(aPosition, 3, it->second.Position);
    if (it->second.Stepped)
        it->second.Position[2] = std::min(cHeight, aPosition[2]);
    // Its capsule meets its furniture; the write-back after physics moves it where it was pushed (actor-movement.md).
    // Actor::Move clears the no-collision bit out of furniture (37507, 0x14068990A).
    if (it->second.Stepped && !it->second.InFurniture)
        it->second.Collides = true;
    if (it->second.Stepped && it->second.InFurniture && it->second.Collides)
        it->second.Position[1] -= kFurnitureOffset * static_cast<float>(1 + it->second.Movements % 3);
    std::copy_n(aRotation, 3, it->second.Rotation);
    // Outdoors its parent cell follows where it stands.
    if (it->second.WorldSpace)
        if (const uint32_t cCell = CellAt(it->second.WorldSpace, {aPosition[0], aPosition[1], aPosition[2]}))
        {
            const uint32_t cFrom = std::exchange(it->second.Cell, cCell);
            CellChanged(aFormId, cFrom);
        }
    return true;
}

float FakeEngine::GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++ValueReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

float FakeEngine::GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++MaximumReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorMaxValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

float FakeEngine::GetActorTemporaryValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    const auto actor = Actors.find(aFormId);
    if (actor == Actors.end())
        return 0.f;
    const auto value = actor->second.TemporaryValues.find(static_cast<uint32_t>(aIndex));
    return value == actor->second.TemporaryValues.end() ? 0.f : value->second;
}

// Its base (SetActorValue's), 0 until set.
float FakeEngine::GetActorBaseValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto cBase = it->second.Bases.find(static_cast<uint32_t>(aIndex));
    return cBase != it->second.Bases.end() ? cBase->second : 0.f;
}

void FakeEngine::SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        ++it->second.ValueWrites;
        const auto cIndex = static_cast<uint32_t>(aIndex);
        auto& base = it->second.Bases[cIndex];
        const float cDelta = aValue - base;
        if (cDelta != 0.f)
        {
            base = aValue;
            it->second.Values.ActorValuesList[cIndex] += cDelta;
            it->second.Values.ActorMaxValuesList[cIndex] += cDelta;
            World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
        }
    }
}

// Permanent and temporary modifiers move the current; damage cannot heal above their combined maximum.
void FakeEngine::ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    auto& values = it->second.Values;
    ++it->second.ValueWrites;
    const auto cBefore = values;
    const auto cIndex = static_cast<uint32_t>(aIndex);
    if (aForceMode == 0)
    {
        values.ActorValuesList[cIndex] += aValue - values.ActorMaxValuesList[cIndex];
        values.ActorMaxValuesList[cIndex] = aValue;
    }
    else if (aForceMode == 1)
    {
        it->second.TemporaryValues[cIndex] += aValue - values.ActorValuesList[cIndex];
        values.ActorValuesList[cIndex] = aValue;
    }
    else if (aForceMode == 2)
        values.ActorValuesList[cIndex] = std::min(aValue, values.ActorMaxValuesList[cIndex] + it->second.TemporaryValues[cIndex]);
    if (cBefore != values)
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
    if (cIndex == kHealth)
        CheckHealth(aFormId, it->second);
}

bool FakeEngine::IsActorStillLoading(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Initializing;
}

bool FakeEngine::IsActorDisabled(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Disabled;
}

bool FakeEngine::IsActorDeleted(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Deleted;
}

// Every enable is TESObjectREFR::Enable (19800), which the plugin refuses for a hidden copy.
void FakeEngine::EnableActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end() && !it->second.Hidden)
        it->second.Disabled = false;
}

void FakeEngine::EnableParentLoads(uint32_t aParent, bool aDisabled)
{
    for (auto& [cId, actor] : Actors)
        if (actor.EnableParent == aParent && !(actor.Hidden && !aDisabled))
            actor.Disabled = aDisabled;
}

void FakeEngine::SetActorHidden(uint32_t aFormId, bool aHidden) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Hidden = aHidden;
        it->second.Disabled |= aHidden;
    }
}

void FakeEngine::DisableActor(uint32_t aFormId, bool aDelete) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Disabled = true;
        it->second.Deleted |= aDelete;
        it->second.HeadTints.reset();
    }
}

void FakeEngine::MarkForDelete(uint32_t aFormId)
{
    Actor& actor = Actors.at(aFormId);
    actor.Deleted = true;
    actor.NoProcess = true;
    actor.Loaded = false;
}

// A copy: a new temporary actor of that NPC, where this game's player is (as PlaceAtMe from the player).
uint32_t FakeEngine::SpawnActorFromNpc(uint32_t aNpcFormId) noexcept
{
    ++Spawned[aNpcFormId];
    const uint32_t cId = NewTemporary();
    Actors[cId] = Actor{.Base = aNpcFormId, .Cell = Actors.at(kPlayer).Cell, .WorldSpace = Actors.at(kPlayer).WorldSpace,
                        .Created = true, .Essential = EssentialBases.contains(aNpcFormId), .Made = m_made};
    return cId;
}

namespace
{
// The race a player's appearance data holds (PlayerSwitchesRace writes "race <id>"), 0 for none.
uint32_t RaceInLooks(const std::string& acAppearance)
{
    uint32_t race = 0;
    return std::sscanf(acAppearance.c_str(), "race %X", &race) == 1 ? race : 0;
}
} // namespace

// A player's copy: a temporary NPC made from their appearance, and an actor of it.
// A base built from a snapshot: the name only if its section (0x20) came, as FormState::Load.
void FakeEngine::LoadBase(uint32_t aBase, const std::string& acAppearance, uint32_t aChangeFlags)
{
    auto& base = Npcs[aBase];
    const size_t cNameEnd = aChangeFlags & 0x20 ? acAppearance.find('\0') : std::string::npos;
    if (cNameEnd != std::string::npos)
        base.Name = acAppearance.substr(0, cNameEnd);
    base.Looks = cNameEnd == std::string::npos ? acAppearance : acAppearance.substr(cNameEnd + 1);
}

uint32_t FakeEngine::CreateCustomActor(const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept
{
    const uint32_t cBase = NewTemporary();
    Npcs[cBase].Weight = aWeight;
    LoadBase(cBase, acAppearance, aChangeFlags);
    Npcs[cBase].Race = RaceInLooks(Npcs[cBase].Looks);
    return SpawnActorFromNpc(cBase);
}

uint32_t FakeEngine::GetActorNpcTemplateBaseFormId(uint32_t aFormId) const noexcept
{
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it == Npcs.end() ? 0 : it->second.FaceBase;
}

uint32_t FakeEngine::SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept
{
    if (aFormId == kPlayer)
    {
        // As native FormState: the name only in its section (0x20), which only naming marks.
        aBuffer = Player.BaseFlags & 0x20 ? PlayerCharacterName() + '\0' + Player.Base : Player.Base;
        return Player.BaseFlags;
    }
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    if (it == Npcs.end())
        return 0;
    const uint32_t cFlags = NpcSnapshot::SaveFlags(it->second.ChangeFlags, false,
                                                 GetActorBaseFormId(aFormId) >= 0xFF000000, it->second.FaceBase != 0);
    // Native FormState saves the full name only when its section (0x20) was requested.
    if (cFlags & 0x20)
        aBuffer = it->second.Name + '\0' + it->second.Looks;
    else if (cFlags)
        aBuffer = "native non-name sections";
    return cFlags;
}

// TESForm::MarkChanged: the sections are saved from now on.
void FakeEngine::MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept
{
    if (aFormId == kPlayer)
        Player.BaseFlags |= aFlags;
    else if (const auto it = Npcs.find(GetActorBaseFormId(aFormId));
             it != Npcs.end() && GetActorBaseFormId(aFormId) < 0xFF000000)
        it->second.ChangeFlags |= aFlags;
}

// The player's is a travel (MoveTo_Impl queues it as a door's): the cells load and unload, a teleport is reported.
void FakeEngine::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    // MoveTo tears down the native mount interaction, including when the moved reference is the horse.
    std::erase_if(Mounts, [&](const auto& acMount) { return acMount.first == aFormId || acMount.second == aFormId; });
    const uint32_t cFrom = it->second.Cell;
    const bool cHad3D = ActorHas3D(aFormId);
    const auto cExterior = Exteriors.find(aCellId);
    it->second.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    // Outdoors the cell at the position, as MoveTo picks it.
    const uint32_t cAt = it->second.WorldSpace ? CellAt(it->second.WorldSpace, {aX, aY, aZ}) : 0;
    it->second.Cell = cAt ? cAt : aCellId;
    it->second.Position[0] = aX;
    it->second.Position[1] = aY;
    it->second.Position[2] = aZ;
    // An NPC moved to another cell drops its 3D, and with it its hand spells (seen on a player's copy, 2026-11-04).
    if (aFormId != kPlayer && it->second.Cell != cFrom && cHad3D)
    {
        RevertSelectedSpells(aFormId);
        DropHead(aFormId);
    }
    CellChanged(aFormId, cFrom);
    // A created actor landing in a cell not loaded goes as with its cell's unload (measured: a player's copy spawned
    // four cells off is gone that frame, before SetActorPersistent).
    if (aFormId != kPlayer && it->second.Created && !it->second.Persistent && !Attached.contains(it->second.Cell))
    {
        DeleteActor(aFormId);
        return;
    }
    // Enabled, UpdateProcessLevel (37501) frees it; disabled, its cell's unload does (TESObjectCELL 19113).
    if (it->second.Collectable && !it->second.Persistent && !Attached.contains(it->second.Cell))
    {
        Actors.erase(it);
        return;
    }
    if (aFormId == kPlayer)
    {
        // Party travel announces it (PlayerCharacter.cpp's 40438), but another game's party call's move.
        if (World::Get().GetTransport().IsOnline() && !m_mirroredMove && !m_unannouncedMove)
            World::Get().GetRunner().Trigger(PlayerTravelQueuedEvent{
                it->second.WorldSpace, it->second.WorldSpace ? 0u : it->second.Cell, {aX, aY, aZ}, Heading});
        World::Get().ctx().at<FakeDialogue&>().End();
        LoadAroundPlayer(cFrom);
        Teleported = true;
        ++Travels;
    }
}

void FakeEngine::CellChanged(uint32_t aFormId, uint32_t aFrom)
{
    const Actor& cActor = Actors.at(aFormId);
    const auto cLocation = [&](uint32_t aCell) {
        const auto it = Locations.find(aCell);
        return it != Locations.end() ? it->second : 0u;
    };
    if (aFormId == kPlayer && cLocation(aFrom) != cLocation(cActor.Cell))
        RaisesStoryEvent(StoryEvent::TypeOf("CLOC"), {kPlayer, cLocation(aFrom), cLocation(cActor.Cell)});
}

void FakeEngine::RaisesStoryEvent(uint32_t aType, std::vector<uint64_t> aMembers)
{
    const auto it = std::ranges::find(StoryTypes, aType, &RegisteredStory::Id);
    if (DecidesWorld || it == StoryTypes.end())
        return;
    OwnStoryEvent event{static_cast<uint32_t>(it - StoryTypes.begin())};
    std::ranges::copy(aMembers | std::views::take(event.Members.size()), event.Members.begin());
    World::Get().GetRunner().Trigger(event);
}

void FakeEngine::Exterior(uint32_t aWorldSpace, uint32_t aFirstCell, int32_t aRadius)
{
    uint32_t cell = aFirstCell;
    for (int32_t x = -aRadius; x <= aRadius; ++x)
        for (int32_t y = -aRadius; y <= aRadius; ++y)
            Exteriors[cell++] = {aWorldSpace, x, y};
}

uint32_t FakeEngine::LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept
{
    for (const auto& [cId, cCell] : Exteriors)
        if (cCell.WorldSpace == aWorldSpaceFormId && cCell.X == aX && cCell.Y == aY)
        {
            if (!AtMainLoop) WorkerCellLoads.push_back(cId);
            return cId;
        }
    return 0;
}

uint32_t FakeEngine::CellAt(uint32_t aWorldSpace, const glm::vec3& acPosition) const noexcept
{
    const auto cAt = GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y);
    // Native movement resolves an already known cell; the bridge lookup can load one from disk.
    for (const auto& [cId, cCell] : Exteriors)
        if (cCell.WorldSpace == aWorldSpace && cCell.X == cAt.X && cCell.Y == cAt.Y)
            return cId;
    return 0;
}

// The loaded grid's centre is the player's cell (TES::centerGridX/Y; the engine's own lag behind it not modelled).
void FakeEngine::LoadAroundPlayer(uint32_t aFrom)
{
    const auto& cPlayer = Actors.at(kPlayer);
    std::set<uint32_t> cells{cPlayer.Cell};
    if (const auto cCentre = Exteriors.find(cPlayer.Cell); cCentre != Exteriors.end())
        for (const auto& [cId, cCell] : Exteriors)
            if (cCell.WorldSpace == cCentre->second.WorldSpace &&
                GridCellCoords::IsCellInGridCell({cCell.X, cCell.Y}, {cCentre->second.X, cCentre->second.Y}, false))
                cells.insert(cId);
    std::set<uint32_t> left = std::exchange(m_playerCells, cells);
    left.insert(aFrom);
    for (const uint32_t cCell : left)
        if (!cells.contains(cCell))
            UnloadCell(cCell);
    for (const uint32_t cCell : cells)
        LoadCell(cCell);
}

void FakeEngine::PlayerStands(uint32_t aCell, const glm::vec3& acPosition)
{
    auto& player = Actors.at(kPlayer);
    const uint32_t cFrom = player.Cell;
    player.Cell = aCell;
    const auto cExterior = Exteriors.find(aCell);
    player.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    std::copy_n(&acPosition.x, 3, player.Position);
    LoadAroundPlayer(cFrom);
}

void FakeEngine::PlayerWalks(const glm::vec3& acPosition)
{
    const auto& cPlayer = Actors.at(kPlayer);
    PlayerStands(CellAt(cPlayer.WorldSpace, acPosition), acPosition);
}

void FakeEngine::SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Rotation[0] = aX;
        it->second.Rotation[2] = aZ;
    }
}

// As SkyrimBridge's: only a reference made at run time is deleted, never the plugins' or the player.
void FakeEngine::DeleteActor(uint32_t aFormId) noexcept
{
    if (!IsActorTemporary(aFormId) || !Actors.erase(aFormId))
        return;
    m_freedTemporary.insert(aFormId);
}

bool FakeEngine::IsActorNpcTemporary(uint32_t aFormId) const noexcept
{
    return GetActorBaseFormId(aFormId) >= 0xFF000000;
}

Inventory FakeEngine::GetActorInventory(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Items : Inventory{};
}

void FakeEngine::SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    // As AddOrRemoveItem: an item with no form here is not added, a coating with no poison here is lost.
    it->second.Items = acInventory;
    std::erase_if(it->second.Items.Entries, [](const Inventory::Entry& acEntry) { return !::ItemForm(acEntry.BaseId); });
    for (auto& entry : it->second.Items.Entries)
        if (entry.ExtraPoisonId && !::ItemForm(entry.ExtraPoisonId))
            entry.ExtraPoisonId = {}, entry.ExtraPoisonCount = 0;
    EquipSpells(aFormId, acInventory.CurrentMagicEquipment); // Actor::SetMagicEquipment
}

// Each value listed; the others stay. As Actor::SetActorValues: a pool's maximum and current; any other value's base
// made the current one, its maximum with it.
void FakeEngine::SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    const auto cPool = [](uint32_t aIndex) { return aIndex >= kHealth && aIndex <= kStamina; };
    for (const auto& [cIndex, cValue] : acValues.ActorMaxValuesList)
        if (cPool(cIndex))
            ForceActorValue(aFormId, 0, cIndex, cValue);
    for (const auto& [cIndex, cValue] : acValues.ActorTemporaryValuesList)
        if (cPool(cIndex))
            ForceActorValue(aFormId, 1, cIndex, GetActorValue(aFormId, cIndex) + cValue - GetActorTemporaryValue(aFormId, cIndex));
    for (const auto& [cIndex, cValue] : acValues.ActorValuesList)
        if (cPool(cIndex))
            ForceActorValue(aFormId, 2, cIndex, cValue);
        else
            SetActorValue(aFormId, cIndex, cValue);
}

// Resurrect: alive, its pools full (its maximums kept), its death action over, reported as the Resurrect hook reports
// it; placement is the party follower move.
void FakeEngine::RevivePlayer() noexcept
{
    auto& player = Actors.at(kPlayer);
    if (!player.Dead)
        return;
    player.Dead = false;
    player.DyingFrames = player.DeadFrames = 0;
    for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
    {
        player.Values.ActorValuesList[cIndex] = player.Values.ActorMaxValuesList[cIndex];
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{kPlayer, cIndex});
    }
    const auto cAction = std::exchange(player.LatestAnimation, {});
    World::Get().GetRunner().Trigger(ActorDeathChangeEvent{kPlayer, true, true,
        cAction.ActionId && DeathActions::IsDying(cAction.State1) ? std::optional<uint64_t>{cAction.Tick} : std::nullopt});
}

void FakeEngine::HoldDeathControls(bool aHold) noexcept
{
    constexpr uint32_t flags = 0x1 | 0x4 | 0x20 | 0x40 | 0x80 | 0x400;
    if (aHold && !DeathControlsHeld)
    {
        DeathControlsBefore = Controls & flags;
        Holds.Take(0xFFFFFFFE, flags);
        Controls &= ~flags;
    }
    else if (!aHold && DeathControlsHeld)
        Controls |= Holds.GiveBack(0xFFFFFFFE, flags) & DeathControlsBefore;
    DeathControlsHeld = aHold;
}

// PartyNatives' entry points (Papyrus/PartyNatives.h) on the current game's fake engine.
namespace PartyNatives
{
void SetInSession(bool aInSession) noexcept
{
    FakeEngine::Current().InSession = aInSession;
}

bool InSession() noexcept
{
    return FakeEngine::Current().InSession;
}

bool Run(const LocalCall& acCall, uint32_t aSource, uint32_t aKey) noexcept
{
    auto& engine = FakeEngine::Current();
    if (aKey)
        engine.Copies.Sent(aSource, aKey);
    engine.Queue(acCall, MirroredTag(aSource));
    return true;
}

void EndCopy(uint32_t aSource, uint32_t aKey, const LocalValue& acResult) noexcept
{
    auto& engine = FakeEngine::Current();
    if (const auto cStack = engine.Copies.Ended(aSource, aKey, acResult))
        engine.AnswerBox(*cStack, std::get<int32_t>(acResult));
}

void ForgetCopies(std::optional<uint32_t> aSource) noexcept
{
    auto& engine = FakeEngine::Current();
    if (aSource)
        engine.Copies.Left(*aSource);
    else
        engine.Copies = {};
}

void GiveBackControls(std::optional<uint32_t> aSource) noexcept
{
    auto& engine = FakeEngine::Current();
    for (const auto& cReturned : engine.Holds.Leave(aSource ? std::optional(MirroredTag(*aSource)) : std::nullopt))
    {
        if (!cReturned.Free)
            continue;
        const auto c = EnableFor(cReturned.Free, cReturned.NoneElse);
        engine.Queue(
            EnablePlayerControls(c.Movement, c.Fighting, c.CamSwitch, c.Looking, c.Sneaking, c.Menu, c.Activate),
            cReturned.Holder);
    }
}
} // namespace PartyNatives

// SystemQuests' seam: this game's list and instances (FakeEngine's).
namespace SystemQuests
{
bool Learn(uint32_t aQuest, bool aTell) noexcept
{
    auto& engine = FakeEngine::Current();
    if (!aQuest || !engine.SystemQuestList.Learn(aQuest))
        return false;
    if (const auto it = engine.QuestGlobals.find(aQuest); it != engine.QuestGlobals.end())
        engine.SystemQuestList.SetGlobals(aQuest, it->second);
    if (aTell)
        World::Get().GetRunner().Trigger(SystemQuestLearnedEvent{aQuest});
    return true;
}

bool Any() noexcept
{
    return FakeEngine::Current().SystemQuestList.Any();
}

bool Is(uint32_t aQuest) noexcept
{
    return FakeEngine::Current().SystemQuestList.Contains(aQuest);
}

bool IsGlobal(uint32_t aGlobal) noexcept
{
    return FakeEngine::Current().SystemQuestList.IsGlobal(aGlobal);
}

std::vector<uint32_t> All() noexcept
{
    return FakeEngine::Current().SystemQuestList.All();
}

void Clear() noexcept
{
    FakeEngine::Current().SystemQuestList.Clear();
}

void TimerCalled(uint32_t aQuest, std::string_view aNative, float aInterval) noexcept
{
    auto& engine = FakeEngine::Current();
    if (engine.SystemQuestList.Contains(aQuest))
        engine.QuestTimers[aQuest][std::string(aNative)] = {std::string(aNative), aInterval};
}

std::string ReadState() noexcept
{
    return FakeEngine::Current().ReadSystemQuests();
}

void ApplyState(std::string_view aState) noexcept
{
    FakeEngine::Current().ApplySystemQuests(aState);
}
} // namespace SystemQuests

void FakeEngine::PlacesMarker(const glm::vec3& acPosition, uint32_t aWorldSpace)
{
    Vector3_NetQuantize position;
    position.x = acPosition.x;
    position.y = acPosition.y;
    position.z = acPosition.z;
    World::Get().GetRunner().Trigger(SetWaypointEvent(position, aWorldSpace));
    Waypoint = Marker{acPosition, aWorldSpace};
}

void FakeEngine::ChoosesDifficulty(uint32_t aDifficulty)
{
    Difficulty = aDifficulty;
    World::Get().GetRunner().Trigger(DifficultyChangeEvent{aDifficulty});
}

void FakeEngine::PlayerSwitchesRace(uint32_t aRace)
{
    Npcs[Actors.at(kPlayer).Base].Race = aRace;
    Player.Base = std::format("race {:X}", aRace);
    Player.BaseFlags |= 0x2000000; // CHANGE_NPC_RACE
    if (InSession)
        World::Get().GetRunner().Trigger(PlayerFormChangeEvent{});
}

void FakeEngine::PlayerUsesRaceMenu(const std::function<void()>& acChanges)
{
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", true});
    acChanges();
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", false});
}

void FakeEngine::PlayerNames(const std::string& acName)
{
    Player.Name = acName;
    Player.BaseFlags |= 0x20;
}

void FakeEngine::AliasNames(uint32_t aFormId, const std::string& acName)
{
    Actors.at(aFormId).DisplayName = acName;
    World::Get().GetRunner().Trigger(ActorRenamedEvent{aFormId});
}

std::string FakeEngine::PlayerId() noexcept
{
    return std::format("id:{:016x}", std::hash<std::string>{}(Name));
}

void FakeEngine::RunMainLoop() noexcept
{
    RunNativeCombat();
    if (const auto cName = std::exchange(PendingSave, std::nullopt))
    {
        if (RefusesSaves || !World::Get().ctx().at<SaveService>().OnSaveRequested())
            MadeSave = std::optional<SaveFiles>{};
        else
        {
            Saves.push_back(*cName);
            Wire::Buffer buffer;
            Wire::Buffer::Writer writer(&buffer);
            // The save holds where its player stands.
            PlayerRecord player = ReadPlayer();
            player.Knowledge = Knowledge;
            const Actor& cPlayer = Actors.at(kPlayer);
            player.Cell = GameId(0, cPlayer.Cell);
            player.Position = {cPlayer.Position[0], cPlayer.Position[1], cPlayer.Position[2]};
            player.Serialize(writer);
            Serialization::WriteVarInt(writer, References.size());
            for (const auto& [id, r] : References)
            {
                Serialization::WriteVarInt(writer, id);
                Serialization::WriteBool(writer, r.Locked);
            }
            // The words' flags (the world's), the player's own factions (its base's, kept with the save).
            for (const auto* pWords : {&WordsKnown, &WordsUnlocked})
            {
                Serialization::WriteVarInt(writer, pWords->size());
                for (const uint32_t cWord : *pWords)
                    Serialization::WriteVarInt(writer, cWord);
            }
            const auto& cFactions = Factions[kPlayer];
            Serialization::WriteVarInt(writer, cFactions.size());
            for (const auto& [cFaction, cRank] : cFactions)
            {
                Serialization::WriteVarInt(writer, cFaction);
                Serialization::WriteVarInt(writer, static_cast<uint32_t>(cRank));
            }
            MadeSave = SaveFiles{{reinterpret_cast<const char*>(buffer.GetData()), writer.Size()}, World::Get().ctx().at<SaveService>().WriteCoSave()};
            SavedFiles[*cName] = **MadeSave;
        }
    }
    const auto cLoad = std::exchange(PendingLoad, std::nullopt);
    if (!cLoad)
        return;
    const auto& [cName, cFiles] = *cLoad;
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(cFiles.Game.data()), cFiles.Game.size()));
    PlayerRecord saved;
    saved.Deserialize(reader);
    const auto count = Serialization::ReadVarInt(reader);
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t id = Serialization::ReadVarInt(reader);
        References[id].Locked = Serialization::ReadBool(reader);
    }
    for (auto* pWords : {&WordsKnown, &WordsUnlocked})
    {
        pWords->clear();
        const auto cWords = Serialization::ReadVarInt(reader);
        for (size_t i = 0; i < cWords; ++i)
            pWords->insert(static_cast<uint32_t>(Serialization::ReadVarInt(reader)));
    }
    auto& factions = Factions[kPlayer];
    factions.clear();
    const auto cFactions = Serialization::ReadVarInt(reader);
    for (size_t i = 0; i < cFactions; ++i)
    {
        const auto cFaction = static_cast<uint32_t>(Serialization::ReadVarInt(reader));
        factions[cFaction] = static_cast<int32_t>(Serialization::ReadVarInt(reader));
    }
    // The save's player's souls: its value in the save (the record sets the joining character's own).
    Souls[kPlayer] = 0.f;
    for (const PlayerRecord::Value& cValue : saved.Values)
        if (cValue.Index == 133)
            Souls[kPlayer] = cValue.Base;
    // The save's system quests: its list (the co-save's) and their instances, the saving game's player's (the VM's).
    SystemQuestList.Clear();
    QuestVariables.clear();
    QuestTimers.clear();
    if (const auto cQuests = saved.Extras.find(SystemQuestState::kExtra); cQuests != saved.Extras.end())
        if (const auto cState = SystemQuestState::Decode(cQuests->second))
        {
            for (const SystemQuestState::Quest& cQuest : cState->Quests)
                SystemQuests::Learn(cQuest.Id.BaseId, false);
            ApplySystemQuests(cQuests->second);
        }
    saved.Extras.erase(SystemQuestState::kExtra);
    Player = saved;
    Knowledge = saved.Knowledge;
    Player.Items = {};
    Player.Cell = {};
    Player.Position = {};
    Actor& player = Actors.at(kPlayer);
    player.Bleeding = player.Dead = false;
    player.DyingFrames = player.DeadFrames = 0;
    player.Values = FullValues();
    player.Items = saved.Items;
    player.Cell = saved.Cell.BaseId;
    std::copy(saved.Position.begin(), saved.Position.end(), player.Position);
    LoadCell(player.Cell);
    auto& saves = World::Get().ctx().at<SaveService>();
    saves.RevertCoSave();
    saves.ReadCoSave(cFiles.CoSave, CharacterRecords::kVersion);
    AtMainMenu = false;
    CharacterId = SaveNames::CharacterId(cName);
    Loaded.push_back(cName);
    World::Get().GetRunner().Trigger(GameLoadedEvent{});
}

bool FakeEngine::Autosaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested(true))
        return false;
    Saves.push_back(acName);
    return true;
}

// As Save_Impl's hook: SaveService decides first.
bool FakeEngine::PlayerSaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested())
        return false;
    Saves.push_back(acName);
    return true;
}

// From the map ("Remove It"): the only removal our hook raises.
void FakeEngine::RemovesMarker()
{
    World::Get().GetRunner().Trigger(RemoveWaypointEvent());
    Waypoint.reset();
}

// HookSetGlobalTimeMultiplier: a change of this game's own, raised while connected.
void FakeEngine::SlowsTime(float aMultiplier)
{
    if (aMultiplier != WantedTimeScale && World::Get().GetTransport().IsConnected())
        World::Get().GetRunner().Trigger(TimeScaleEvent{aMultiplier});
    WantedTimeScale = aMultiplier;
}

void FakeEngine::CastsSpell(uint32_t aCaster, uint32_t aSpell, uint32_t aSource, uint32_t aTarget)
{
    const auto cActor = Actors.find(aCaster);
    if (cActor == Actors.end())
        return;
    if (!cActor->second.Copy)
        World::Get().GetRunner().Trigger(SpellCastEvent(aCaster, aSpell, aTarget, aSource, false, cActor->second.Loaded));
    Casts.push_back({aCaster, aSpell, aSource, aTarget});
}

void FakeEngine::SpellHits(uint32_t aTarget, uint32_t aCaster, uint32_t aSpell, uint32_t aEffect, float aMagnitude)
{
    const auto cTarget = Actors.find(aTarget);
    if (cTarget == Actors.end())
        return;
    const bool cOurCast = Actors.contains(aCaster) && !Actors.at(aCaster).Copy;
    if (cTarget->second.Copy)
    {
        if (!cOurCast)
            return;
        AddTargetEvent event;
        event.TargetID = aTarget;
        event.CasterID = aCaster;
        event.SpellID = aSpell;
        event.EffectID = aEffect;
        event.Magnitude = aMagnitude;
        World::Get().GetRunner().Trigger(event);
        return;
    }
    Effects.push_back({aTarget, aCaster, aSpell, aEffect});
}

void FakeEngine::Fights(uint32_t aActor, uint32_t aTarget)
{
    auto& ai = Actors.at(aActor).Ai;
    ai.CombatState = aTarget ? 1 : 0;
    ai.CombatTargetId = aTarget;
    World::Get().GetRunner().Trigger(AiStateChangeEvent{aActor});
}

void FakeEngine::AttackedBy(uint32_t aVictim, uint32_t aAttacker)
{
    // HookStartCombat's check (38561); its wiring is checked in Skyrim (scenario host-dies-guest-still-fought).
    auto& victim = Actors.at(aVictim);
    if (CopyCombat::MayStart(victim.Copy))
        victim.OwnCombatTarget = aAttacker;
}

void FakeEngine::LooksAt(uint32_t aActor, uint32_t aSlot, uint32_t aTarget)
{
    auto& slot = Actors.at(aActor).Ai.HeadTrackIds.at(aSlot);
    if (std::exchange(slot, aTarget) != aTarget)
        World::Get().GetRunner().Trigger(AiStateChangeEvent{aActor});
}

void FakeEngine::Burns(uint32_t aActor, uint32_t aEffect, bool aOn)
{
    auto& visuals = Actors.at(aActor).Visuals;
    aOn ? (void)visuals.insert(aEffect) : (void)visuals.erase(aEffect);
    World::Get().GetRunner().Trigger(EffectVisualsChangeEvent{aActor, 0});
}

std::vector<IActorBridge::ActiveEffectInfo> FakeEngine::GetActorActiveEffects(uint32_t aFormId) const noexcept
{
    std::vector<ActiveEffectInfo> effects;
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        for (const uint32_t cId : it->second.Visuals)
            effects.push_back({.EffectFormId = cId});
    return effects;
}

void FakeEngine::JudgedPlayer(uint32_t aWitness, int32_t aLevel)
{
    KnowledgeOfPlayer[aWitness] = {DetectionRules::Pass{.Level = aLevel, .LineOfSight = true, .Seen = true}, DetectionFrame, false};
}

void FakeEngine::ForcesDetected(uint32_t aWitness)
{
    auto& known = KnowledgeOfPlayer[aWitness];
    const auto cBefore = known.State.value_or(DetectionRules::Pass{});
    known.State = DetectionRules::Pass{.Level = 100, .LineOfSight = cBefore.LineOfSight, .Seen = cBefore.Seen, .Unk16 = 2};
    known.UpdatedAt = DetectionFrame;
}

const DetectionRules::Pass* FakeEngine::FreshState(uint32_t aWitness)
{
    // Priority 3: refreshed after 1 s, answered unseen after 2 s (table 0x1420B8AB8), at 60 frames a second.
    auto& known = KnowledgeOfPlayer[aWitness];
    if (!known.State || known.Queued)
    {
        known.Queued = true;
        return known.State ? &*known.State : nullptr;
    }
    const uint32_t cAge = DetectionFrame - known.UpdatedAt;
    if (cAge > 60)
        known.Queued = true;
    return cAge > 120 ? nullptr : &*known.State;
}

int32_t FakeEngine::StealthEye()
{
    int32_t highest = DetectionRules::kUnseen;
    for (const auto& [cId, cActor] : Actors)
        if (cId != kPlayer && cActor.Loaded && !cActor.Dead && !cActor.NotOnStealthMeter)
            if (const auto* cpState = FreshState(cId))
                highest = std::max(highest, cpState->Level);
    return highest;
}

void FakeEngine::ProcessTracking()
{
    for (auto& [cId, actor] : Actors)
    {
        if (cId == kPlayer || !actor.Loaded || actor.Dead || actor.KnockedDown)
            continue;
        auto& slots = actor.Ai.HeadTrackIds;
        const auto cTargets = HeadTrackRead::Targets(slots, cId);
        if (cTargets.empty() || cTargets[0] != kPlayer || !slots[0] || !DetectionRules::HeadTrackAsksDetection(actor.Copy))
            continue;
        if (const auto* cpState = FreshState(cId); cpState && cpState->Level <= 0)
        {
            slots[0] = 0;
            if (!actor.Copy)
                World::Get().GetRunner().Trigger(AiStateChangeEvent{cId});
        }
    }
}

void FakeEngine::DetectionPass()
{
    for (auto& [cWitness, known] : KnowledgeOfPlayer)
    {
        if (!known.Queued)
            continue;
        const auto cSees = Sees.find(cWitness);
        DetectionRules::Pass computed{};
        if (cSees != Sees.end())
            computed = {.Level = cSees->second, .LineOfSight = true, .Seen = true};
        known = {DetectionRules::Installed(PartyOf(cWitness) == HitRules::Party::OtherPlayer, true, computed), DetectionFrame, false};
    }
}

HitRules::Party FakeEngine::PartyOf(uint32_t aFormId) const noexcept
{
    using enum HitRules::Party;
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return None;
    if (aFormId == kPlayer)
        return LocalPlayer;
    if (it->second.Copy)
        return it->second.Player ? OtherPlayer : Copy;
    return Local;
}

void FakeEngine::TakesDamage(uint32_t aHittee, uint32_t aHitter, float aDamage, bool aApplyingEffect)
{
    switch (HitRules::Of(PartyOf(aHittee), PartyOf(aHitter), aApplyingEffect))
    {
    case HitRules::Route::Apply:
        World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage));
        DamageActorHealth(aHittee, aHitter, aDamage);
        break;
    case HitRules::Route::AskOwner: World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage, aHitter)); break;
    case HitRules::Route::Mirror:
    case HitRules::Route::Drop: break;
    }
}

void FakeEngine::Strikes(uint32_t aHitter, uint32_t aHittee, float aDamage)
{
    // HookDamageActor's check.
    if (const auto& cHitter = Actors.at(aHitter);
        ::ReplayState::CancelledBlow(World::Get().GetTick() < cHitter.ClashUntil))
        return;
    if (!Actors.contains(aHittee))
        return;
    World::Get().GetRunner().Trigger(HitEvent(aHitter, aHittee));
    TakesDamage(aHittee, aHitter, aDamage, false);
}

void FakeEngine::Shoots(uint32_t aShooter, uint32_t aSpell, const glm::vec3& acOrigin)
{
    ProjectileLaunchedEvent event{};
    event.Origin = acOrigin;
    event.ShooterID = aShooter;
    event.SpellID = aSpell;
    event.ParentCellID = Actors.at(aShooter).Cell;
    World::Get().GetRunner().Trigger(event);
    Shots.push_back({aShooter, aSpell, acOrigin});
}

void FakeEngine::Activates(uint32_t aActor, uint32_t aObject)
{
    ActivateEvent event;
    event.ObjectFormId = aObject;
    event.ActivatorFormId = aActor;
    event.ObjectParentCellFormId = Actors.contains(aObject) ? Actors.at(aObject).Cell : References.at(aObject).Cell;
    World::Get().GetRunner().Trigger(event);
    Activations.emplace_back(aObject, aActor);
    // The player activating a corpse opens its loot (TESNPC::Activate -> ContainerMenu::OpenMenu, our hook first).
    if (aActor == kPlayer && Actors.contains(aObject) && IsActorDead(aObject) && !LootMenuOf &&
        !ContainerOpens::Refused(Actors.at(aObject).Copy && Actors.at(aObject).Player))
    {
        LootMenuOf = aObject;
        World::Get().GetRunner().Trigger(MenuEvent{"ContainerMenu", true});
    }
    ToggleDoor(aObject);
}

// The alias clear hooks report their quest after the engine removes the fill, without a stage/flag event.
void FakeEngine::ClearReferenceAlias(uint32_t aQuest, uint32_t aAlias)
{
    for (auto& fill : Quests.at(aQuest).Aliases)
        if (fill.AliasId == aAlias)
        {
            fill = QuestAliasFill{aAlias};
            QuestChanged(aQuest);
            return;
        }
}

void FakeEngine::ClearLocationAlias(uint32_t aQuest, uint32_t aAlias)
{
    for (auto& fill : Quests.at(aQuest).Locations)
        if (fill.AliasId == aAlias)
        {
            fill = QuestLocationFill{aAlias};
            QuestChanged(aQuest);
            return;
        }
}

// StatsMenu::ModifyPerkPoints writes directly; its hook marks both default globals after either signed delta.
void FakeEngine::ModifyBeastPerks(bool aVampire, int32_t aDelta)
{
    Globals.at(BeastPerkGlobals[aVampire ? 1 : 0]) += static_cast<float>(aDelta);
    GlobalWrites.insert(BeastPerkGlobals.begin(), BeastPerkGlobals.end());
}

std::optional<std::string> FakeEngine::PreviousSave() noexcept
{
    for (auto i = Saves.rbegin(); i != Saves.rend(); ++i)
        if (!i->starts_with("SkyrimCoop") && SaveNames::CharacterId(*i) == CharacterId && SavedFiles.contains(*i))
            return *i;
    return std::nullopt;
}

std::optional<IJoinBridge::SaveFiles> FakeEngine::ReadSave(const std::string& acName) noexcept
{
    const auto it = SavedFiles.find(acName);
    return it != SavedFiles.end() ? std::optional(it->second) : std::nullopt;
}

bool FakeEngine::PlayerLoads(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnLoadRequested(acName))
        return false;
    const auto cFiles = ReadSave(acName);
    return cFiles && WriteAndLoad(acName, *cFiles);
}

bool FakeEngine::PlaceBesideHost() noexcept
{
    ++PlacementChecks;
    auto& player = Actors.at(kPlayer);
    const glm::vec3 cAnchor(player.Position[0], player.Position[1], player.Position[2]);
    const auto cSpot = CheckedPlacement::Beside(cAnchor, [&](const glm::vec3& acFrom, const glm::vec3& acTo) {
        if (acTo.z < acFrom.z - 200.f)
            return std::optional{CheckedPlacement::Hit{true, {acFrom.x, acFrom.y, cAnchor.z}, {0.f, 0.f, 1.f}}};
        return std::optional{CheckedPlacement::Hit{!PlacementClear, acTo}};
    });
    if (!cSpot) return false;
    std::copy_n(&cSpot->x, 3, player.Position);
    return true;
}

void FakeEngine::StartsHitVisuals(uint32_t aActor, uint32_t aEffect, float aDuration, uint32_t aCastingType)
{
    const bool cFlash = HitVisualsRules::Flash(EffectVisualFlags[aEffect], true, aDuration, aCastingType);
    World::Get().GetRunner().Trigger(EffectVisualsChangeEvent{aActor, cFlash ? aEffect : 0});
}

bool FakeEngine::ApplyActorLooks(uint32_t aFormId, const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept
{
    ++AppearanceAttempts;
    if (!AtMainLoop || BlockAppearance || !Actors.contains(aFormId) || !Actors.at(aFormId).Copy)
        return false;
    auto& base = Npcs.at(GetActorBaseFormId(aFormId));
    LoadBase(GetActorBaseFormId(aFormId), acAppearance, aChangeFlags);
    const uint32_t cRace = RaceInLooks(base.Looks);
    if (base.Race != cRace)
    {
        auto& actor = Actors.at(aFormId);
        actor.Values = FullValues(); // SwitchRace recalculates values without a discrete value-write event.
        actor.Bases = actor.Values.ActorMaxValuesList;
        actor.TemporaryValues.clear();
    }
    base.Race = cRace;
    base.Weight = aWeight;
    return true;
}

bool FakeEngine::OpenFollowerInventory(uint32_t aNpcFormId) noexcept
{
    if (FollowerTradeNpc || !Actors.contains(aNpcFormId))
        return false;
    FollowerTradeNpc = aNpcFormId;
    World::Get().GetRunner().Trigger(MenuEvent{"ContainerMenu", true});
    return true;
}

void FakeEngine::EndScene(uint32_t aActor)
{
    // TESSceneEvent can be delivered before the final actor scene field is cleared.
    World::Get().GetDispatcher().trigger(AiStateChangeEvent{aActor});
    Actors.at(aActor).Ai.SceneId = 0;
}
