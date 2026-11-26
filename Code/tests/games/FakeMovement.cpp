// FakeEngine: where actors are, how copies are moved, clips and 3D loading.
#include <CriticalStageRules.h>
#include <animation/DeferredClips.h>
#include <DisplayNames.h>
#include <FreedWornList.h>
#include <Diagnostics/HeadTrackRead.h>
#include <CopyCombat.h>
#include <core/MenuEvent.h>
#include <magic/InterruptCastEvent.h>
#include <characters/ActorHeadUpdateEvent.h>
#include <CheckedPlacement.h>
#include <core/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include <movement/CopyStep.h>
#include <magic/OwnerEffects.h>
#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>
#include <mounts/MountEvent.h>
#include <join/SaveWrittenEvent.h>
#include <animation/GraphState.h>
#include <animation/GraphStream.h>
#include <bit>
#include <set>
#include <span>
#include <characters/ActorRenamedEvent.h>
#include "FakeDialogue.h"
#include <core/PlayerTravelQueuedEvent.h>
#include <scripts/StoryEvents.h>
#include <items/ContainerChangeEvent.h>
#include <core/DifficultyChangeEvent.h>
#include <world/ActivateEvent.h>
#include <items/ContainerOpens.h>
#include <death/DeathDrops.h>
#include <combat/AddTargetEvent.h>
#include <magic/SummonEvent.h>
#include <actorstate/AiStateChangeEvent.h>
#include <magic/EffectVisualsChangeEvent.h>
#include <items/EquipmentChangeEvent.h>
#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorDamagedEvent.h>
#include <characters/ActorValueChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <characters/ReferenceLoadedEvent.h>
#include <characters/NpcHeldEvent.h>
#include <GuestNpcs.h>
#include <actorstate/DeathActions.h>
#include <ranges>
#include <core/World.h>

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

// A clip whose files its process does not hold lasts no time (59263): its graph runs on to the next. A clip with
// prop triggers holds its props as it starts (AnimationObjects::Load 43571, Draw 43572).
void FakeEngine::EnterClip(Actor& aActor) const
{
    if (const auto cProps = StateProps.find(aActor.GraphState); cProps != StateProps.end())
        HoldAnimObjects(aActor, cProps->second);
    for (const auto& [cEvent, cState] : ClipFiles)
        if (cState == aActor.GraphState && !aActor.LoadedClips.contains(cEvent))
            if (const auto cNext = SkippedClips.find(aActor.GraphState); cNext != SkippedClips.end())
            {
                aActor.GraphState = cNext->second;
                return;
            }
}

void FakeEngine::HoldAnimObjects(Actor& aActor, std::span<const std::pair<uint32_t, bool>> aProps)
{
    for (const auto& [cObject, cDrawn] : aProps)
    {
        // One entry per (reference, ANIO); Draw shows a loaded one (43572).
        const auto it = std::ranges::find(aActor.AnimObjects, cObject, &std::pair<uint32_t, bool>::first);
        if (it == aActor.AnimObjects.end())
            aActor.AnimObjects.emplace_back(cObject, cDrawn);
        else
            it->second = it->second || cDrawn;
    }
}

bool FakeEngine::TakeGraphEvent(Actor& aActor, const std::string& acEvent)
{
    // NotifyAllGraphs 63362: its linked furniture's graphs get every event too, taken or not (63372).
    if (aActor.LinkedFurniture && !acEvent.empty())
        FurnitureReceives(aActor.LinkedFurniture, acEvent);
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
    if (const auto cOut = StateOutputs.find(aActor.GraphState); cOut != StateOutputs.end() && aActor.LinkedFurniture)
        FurnitureReceives(aActor.LinkedFurniture, cOut->second);
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
    graph.Pose.assign(1, PoseBone{});
    graph.Pose[0].Translation[0] = static_cast<float>(graph.Timeline);
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

// One graph update (63358): channels, then the step; an owner's recorded for its copies (engine/Animation's hooks).
void FakeEngine::UpdateGraph(uint32_t aFormId, Actor& aActor)
{
    const auto cStep = float(aActor.AnimationStepMs); // the stream carries it as a float, as Skyrim's deltaTime
    WriteGraphChannels(aActor, cStep);
    auto& streams = World::Get().GetGraphStreams();
    // In a pair or a kill move it is recorded on, but gives no sync point (the engine's 63587 hook).
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
                                   .ClipSets = std::move(sets), .AnimObjects = aActor.AnimObjects});
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
        Actor.WeaponEventIn = 0; // its own draw's weaponDraw went with the graph it replaced
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
    std::span<const PoseBone> GeneratedPose() override { return Actor.Graph.Pose; }
    void ShowPose(std::span<const PoseBone> aPose) override { Actor.Graph.Pose.assign(aPose.begin(), aPose.end()); }
    void HoldAnimObjects(std::span<const std::pair<uint32_t, bool>> aObjects) override { FakeEngine::HoldAnimObjects(Actor, aObjects); }
    std::shared_ptr<std::atomic<bool>> RequestClips(uint8_t aSet, uint16_t aEventId) override
    {
        const auto cEvents = Engine.GraphEventTable();
        if (aEventId >= cEvents.size())
            return nullptr;
        // As the engine's target: between frames, not in the graph's update.
        const auto cCopy = [pEngine = &Engine, cFormId = FormId] {
            const auto cActor = pEngine->Actors.find(cFormId);
            return cActor != pEngine->Actors.end() && cActor->second.Copy;
        };
        return DeferClipRequest(cCopy, [pEngine = &Engine, cFormId = FormId, event = cEvents[aEventId], aSet](const auto& acDone) {
            return (aSet == 47 ? pEngine->RequestMovementClips(cFormId, event, acDone) : pEngine->RequestActionClips(cFormId, event, acDone)) != nullptr;
        });
    }
};
} // namespace

void FakeEngine::ReplayStream(uint32_t aFormId, Actor& aActor)
{
    FakeGraphTarget target(*this, aFormId, aActor);
    m_replaying = true;
    World::Get().GetGraphStreams().Replay(aFormId, target, GraphIdentity(aActor), aActor.GraphProject);
    m_replaying = false;
    aActor.Graph.Updated = false; // its generate passes are its owner's, in the replay (the engine's 63587 hook)
}

void FakeEngine::ReloadGraph(uint32_t aFormId, const std::string& aState, uint32_t aProject)
{
    auto& actor = Actors.at(aFormId);
    ++actor.GraphGeneration;
    actor.WeaponEventIn = 0;
    if (aProject)
        actor.GraphProject = aProject;
    actor.GraphState = aState;
    actor.GraphVariables = {};
    actor.Graph.ClipStartedAt.reset();
    actor.Graph.Stuck = false;
    actor.Graph.Matches.clear();
    GraphEntry(actor, "reload");
}

void FakeEngine::ShowWeather(const GameId& acWeather, bool aFade) noexcept
{
    if (aFade && Weather)
    {
        LastWeather = Weather;
        FadeFrames = 0;
    }
    else
    {
        WeatherCuts += Weather ? 1 : 0;
        LastWeather = {};
    }
    Weather = acWeather;
    WeatherAge = 0;
    ++WeatherChanges;
    for (const auto& [cRegion, cList] : RegionLists)
        SetRegionWeather(cRegion);
}

void FakeEngine::SetRegionWeather(uint32_t aRegion) noexcept
{
    const auto cList = RegionLists.find(aRegion);
    if (cList == RegionLists.end() || cList->second.empty())
        RegionWeather.erase(aRegion);
    else
        RegionWeather[aRegion] = std::ranges::contains(cList->second, Weather) ? Weather : cList->second.front();
}

// The movement job (HookApplyMovement) steps a copy to its last goal until it is given another, at up to kTopStep a frame
// (Skyrim 2026-11-24: 147 units in a frame toward the cell it had left).
void FakeEngine::StepCopiesToGoals() noexcept
{
    for (auto& [cId, actor] : Actors)
    {
        if (!actor.Copy || !actor.Stepped || actor.Dead || actor.Disabled || !actor.Loaded)
            continue;
        const glm::vec3 cAt(actor.Position[0], actor.Position[1], actor.Position[2]);
        const glm::vec3 cOff = glm::vec3(actor.Goal[0], actor.Goal[1], actor.Goal[2]) - cAt;
        const float cLeft = glm::length(glm::vec2(cOff));
        if (cLeft <= CopyStep::kStill)
            continue;
        const glm::vec3 cTo = cAt + cOff * (std::min(cLeft, kTopStep) / cLeft);
        actor.Position[0] = cTo.x;
        actor.Position[1] = cTo.y;
    }
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
    it->second.Stepped = aMove == CopyMove::Step && !RagdollPosed(aFormId);
    if (aMove == CopyMove::Leave || RagdollPosed(aFormId))
        return true;
    ++it->second.Movements;
    const float cHeight = it->second.Position[2];
    std::copy_n(aPosition, 3, it->second.Position);
    if (it->second.Stepped)
    {
        std::copy_n(aPosition, 3, it->second.Goal);
        it->second.Position[0] -= kStepShort;
        // Never up (support and gravity keep its height), resting kRigidRest under its owner; the write-back after
        // physics (HookSetPosition) gives the reference its owner's height on its owner's place.
        const float cGround = std::min(it->second.ControllerZ.value_or(cHeight) + kRigidRest, aPosition[2]);
        it->second.ControllerZ = cGround - kRigidRest;
        const glm::vec3 cAt(it->second.Position[0], it->second.Position[1], *it->second.ControllerZ);
        it->second.Position[2] = CopyStep::RestHeight(cAt, {aPosition[0], aPosition[1], aPosition[2]});
    }
    else
        it->second.ControllerZ = aPosition[2]; // a warp places its capsule there
    // Its capsule meets its furniture; the write-back after physics moves it where it was pushed (actor-movement.md).
    // Actor::Move clears the no-collision bit out of furniture (37507, 0x14068990A).
    if (it->second.Stepped && !it->second.InFurniture)
        it->second.Collides = true;
    if (it->second.Stepped && it->second.InFurniture && it->second.Collides)
        it->second.Position[1] -= kFurnitureOffset * static_cast<float>(1 + it->second.Movements % 3);
    std::copy_n(aRotation, 3, it->second.Rotation);
    // Outdoors its parent cell follows where it stands while that cell is attached: in a detached one it gets no UpdateAI,
    // so no UpdateCellAttachment (37352; Skyrim 2026-11-22, player-copies-after-setpos).
    if (it->second.WorldSpace && Attached.contains(it->second.Cell))
        if (const uint32_t cCell = CellAt(it->second.WorldSpace, {aPosition[0], aPosition[1], aPosition[2]}))
        {
            const uint32_t cFrom = std::exchange(it->second.Cell, cCell);
            CellChanged(aFormId, cFrom);
        }
    return true;
}

// The player's is a travel (MoveTo_Impl queues it as a door's): the cells load and unload, a teleport is reported.
void FakeEngine::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    if (aFormId == kPlayer && DropPlayerMoves)
    {
        --DropPlayerMoves;
        return;
    }
    // MoveTo tears down the native mount interaction, including when the moved reference is the horse.
    std::vector<uint32_t> riders;
    for (const auto& [cRider, cMount] : Mounts)
        if (cRider == aFormId || cMount == aFormId)
            riders.push_back(cRider);
    for (const uint32_t cRider : riders)
        EndRide(cRider);
    const uint32_t cFrom = it->second.Cell;
    const bool cHad3D = ActorHas3D(aFormId);
    const auto cExterior = Exteriors.find(aCellId);
    it->second.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    // Outdoors the cell at the position, as MoveTo picks it.
    const uint32_t cAt = it->second.WorldSpace ? CellAt(it->second.WorldSpace, {aX, aY, aZ}) : 0;
    it->second.Cell = cAt ? cAt : aCellId;
    it->second.Stepped = false; // a moved copy's goal goes with the place it left
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

void FakeEngine::SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Rotation[0] = aX;
        it->second.Rotation[2] = aZ;
    }
}
