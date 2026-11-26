// What the real client links from engine files, for games in this process: World (only the services a flow needs, on
// the fake engine) and RunningGame (the host's game; the plugins the fake engine lists).
#include <CreatedIds.h>
#include <Games/Animation/ReplayState.h>
#include <Games/KnockReplay.h>
#include <SessionIds.h>
#include "FakeDialogue.h"
#include <MainLoopTasks.h>
#include "FakeWait.h"
#include "FakeCrime.h"
#include "FakeBugReports.h"
#include <Services/CrimeService.h>
#include <Services/WaitService.h>
#include <Services/BodyService.h>
#include "FakeSteam.h"
#include "FakeEngine.h"
#include <Games/DeathActions.h>

#include <Bridges/IActorBridge.h>
#include "FakeGuestEngine.h"
#include <IEngine.h>
#include "FakeHostEngine.h"
#include <Services/GuestService.h>

#include <Games/Animation/ActionClaims.h>
#include <Games/Animation/GraphStream.h>
#include <Events/UpdateEvent.h>
#include <RunningGame.h>
#include <Components/RemoteAnimationComponent.h>
#include <Components/FormIdComponent.h>
#include <Components/SpawnGraphStateComponent.h>
#include <Components/WaitingFor3D.h>
#include <Systems/InterpolationSystem.h>
#include <Services/ActorValueService.h>
#include <Services/AiStateService.h>
#include <Services/ActorStateService.h>
#include <Services/ClaimService.h>
#include <Services/KillMoveService.h>
#include <Games/Animation/ActorStateRules.h>
#include <Services/CopyRecords.h>
#include <Services/CopyRecordsService.h>
#include <Services/PerkService.h>
#include <Services/EffectVisualsService.h>
#include <Services/CharacterService.h>
#include <Services/CombatService.h>
#include <Services/CutsceneService.h>
#include <Services/SystemQuestService.h>
#include <Services/BugReportService.h>
#include <Services/DevToolsService.h>
#include "FakeDevTools.h"
#include <Services/DialogueService.h>
#include <Services/HostService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/MapService.h>
#include <Services/PartyMarkerService.h>
#include <Services/ObjectService.h>
#include <Services/TimeScaleService.h>
#include <Services/PlayerService.h>
#include <Services/DeathService.h>
#include <Services/TravelService.h>
#include <Services/RunnerService.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Services/FollowerService.h>
#include <Services/SessionEnd.h>
#include <Services/SessionService.h>
#include <Services/SteamService.h>
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>
#include <World.h>

// The engine a game here runs on: the fake behind every port, and the frame the fake drives.
namespace
{
class FakeEngineSet final : public IEngine
{
public:
    IActorBridge& Actors() noexcept override { return m_engine; }
    IFormBridge& Forms() noexcept override { return m_engine; }
    IPlayerBridge& Player() noexcept override { return m_engine; }
    IWorldBridge& WorldBridge() noexcept override { return m_engine; }
    IDialogueBridge& Dialogue() noexcept override { return m_dialogue; }
    ISteamBridge& Steam() noexcept override { return m_steam; }
    IJoinBridge& Join() noexcept override { return m_engine; }
    IWaitBridge& Wait() noexcept override { return m_wait; }
    IBodyBridge& Bodies() noexcept override { return m_engine; }
    ICrimeBridge& Crime() noexcept override { return m_crime; }
    IBugReportBridge& BugReports() noexcept override { return m_bugReports; }
    IDevToolsBridge& DevTools() noexcept override { return m_devTools; }
    std::unique_ptr<IGuestEngine> MakeGuestEngine() override { return std::make_unique<FakeGuestEngine>(m_engine); }
    std::unique_ptr<IHostEngine> MakeHostEngine() override { return std::make_unique<FakeHostEngine>(*m_pWorld, m_engine); }

    // The fakes themselves, for the flows and the other fakes to reach.
    void AddFirstServices(World& aWorld) override
    {
        m_pWorld = &aWorld;
        aWorld.ctx().emplace<FakeEngine&>(m_engine);
        aWorld.ctx().emplace<FakeWait&>(m_wait);
        aWorld.ctx().emplace<FakeCrime&>(m_crime);
        aWorld.ctx().emplace<FakeBugReports&>(m_bugReports);
        aWorld.ctx().emplace<FakeDevTools&>(m_devTools);
        aWorld.ctx().emplace<FakeDialogue&>(m_dialogue);
        aWorld.ctx().emplace<FakeSteam&>(m_steam);
        // An NPC's AI sets its slot; a copy runs none (Skyrim: the copy's slots stayed empty).
        m_dialogue.SpeakerLooks = [](uint32_t aSpeaker, uint32_t aTarget) {
            FakeEngine& engine = FakeEngine::Current();
            if (const auto it = engine.Actors.find(aSpeaker); it != engine.Actors.end() && !it->second.Copy)
                engine.LooksAt(aSpeaker, FakeEngine::kDialogueHeadTrack, aTarget);
        };
        // An NPC's AI turns it; a copy follows its owner's heading.
        m_dialogue.SpeakerTurns = [](uint32_t aSpeaker, uint32_t aTarget) {
            FakeEngine& engine = FakeEngine::Current();
            const auto it = engine.Actors.find(aSpeaker);
            const auto target = engine.Actors.find(aTarget);
            if (it != engine.Actors.end() && !it->second.Copy && target != engine.Actors.end())
                it->second.Rotation[2] = std::atan2(target->second.Position[0] - it->second.Position[0],
                                                    target->second.Position[1] - it->second.Position[1]);
        };
        m_dialogue.SpeakerHasProcess = [](uint32_t aSpeaker) {
            const FakeEngine& engine = FakeEngine::Current();
            const auto it = engine.Actors.find(aSpeaker);
            return it != engine.Actors.end() && !it->second.NoProcess;
        };
        m_dialogue.SpeakerDeadOrFighting = [](uint32_t aSpeaker) {
            const FakeEngine& engine = FakeEngine::Current();
            const auto it = engine.Actors.find(aSpeaker);
            return it != engine.Actors.end() && (it->second.Dead || it->second.OwnCombatTarget);
        };
        // DialogueBridge::Start refuses a dead or fighting actor.
        m_dialogue.CanTalk = [](uint32_t aActor) {
            const FakeEngine& engine = FakeEngine::Current();
            const auto it = engine.Actors.find(aActor);
            return it == engine.Actors.end() || (!it->second.Dead && !it->second.OwnCombatTarget);
        };
        m_dialogue.SpeakerResolves = [](uint32_t aSpeaker) {
            return FakeEngine::Current().HandleResolves(aSpeaker);
        };
        m_dialogue.DialogueLoaded = [](uint32_t aQuest) {
            return FakeEngine::Current().DialogueLoaded.contains(aQuest);
        };
    }

    void Frame(World& aWorld) noexcept override
    {
        auto& mainEngine = aWorld.ctx().at<FakeEngine&>();
        if (mainEngine.FrameStalled)
            return;
        mainEngine.AtMainLoop = true;
        MainLoopTasks::Run();
        mainEngine.AtMainLoop = false;
        aWorld.ctx().at<FakeEngine&>().RunMainLoop();
        const auto cNow = std::chrono::high_resolution_clock::now();
        const double cDelta = std::chrono::duration<double>(cNow - m_lastFrameTime).count();
        m_lastFrameTime = cNow;
        const double cEngineFrameMs = mainEngine.FrameMs > 0.0 ? mainEngine.FrameMs : cDelta * 1000.0;
        if (mainEngine.FrameMs > 0.0)
            mainEngine.EngineMs = mainEngine.EngineMs > 0.0 ? mainEngine.EngineMs + cEngineFrameMs : static_cast<double>(aWorld.GetTick());
        // A delayed line delivers in wall time: recovery must use that clock too.
        aWorld.Step(std::getenv("COOP_NET") ? cDelta : 1.0 / 60, cEngineFrameMs);
        m_dialogue.EngineFrame(aWorld.GetTransport().IsOnline());
        aWorld.ctx().at<FakeEngine&>().Releasing.clear();
        // The real AnimationSystem replays a dying owner's action at its copy's time (CopyPlayback).
        auto& engine = aWorld.ctx().at<FakeEngine&>();
        for (auto&& [cEntity, animation, form] : aWorld.view<RemoteAnimationComponent, FormIdComponent>().each())
        {
            if (!engine.ActorExists(form.Id)) continue;
            // On the stream, its sync point is newer than its spawn's state (as AnimationSystem).
            if (auto* spawn = aWorld.try_get<SpawnGraphStateComponent>(cEntity); spawn && aWorld.GetGraphStreams().OnStream(form.Id))
            {
                // Its actor state alone, and its spawn action taken (as AnimationSystem).
                if (engine.LoadGraphStateFlags(form.Id, spawn->State) && !animation.TimePoints.empty())
                {
                    animation.LastRanAction = animation.TimePoints.front();
                    engine.SetActorLatestAnimation(form.Id, animation.TimePoints.front());
                    animation.TimePoints.pop_front();
                }
                aWorld.remove<SpawnGraphStateComponent>(cEntity);
            }
            if (auto* spawn = aWorld.try_get<SpawnGraphStateComponent>(cEntity))
            {
                if (aWorld.all_of<WaitingFor3D>(cEntity) || !engine.IsAnimationGraphReady(form.Id)) continue;
                if (engine.LoadActorGraphState(form.Id, spawn->State) && !animation.TimePoints.empty())
                {
                    animation.LastRanAction = animation.TimePoints.front();
                    animation.Variables = animation.LastRanAction.Variables;
                    animation.VariablesTick = animation.LastRanAction.Tick;
                    engine.SetActorLatestAnimation(form.Id, animation.TimePoints.front());
                    animation.TimePoints.pop_front();
                }
                aWorld.remove<SpawnGraphStateComponent>(cEntity);
            }
            auto& actor = engine.Actors.at(form.Id);
            // On its owner's graph input stream (graph-input-stream.md), as AnimationSystem: its graph's inputs are the
            // stream's, so its actions are bookkeeping only, but a death (its own body's).
            auto& streams = aWorld.GetGraphStreams();
            const uint64_t cPlayback = InterpolationSystem::PlaybackTick(aWorld, cEntity);
            streams.SetPlayback(form.Id, cPlayback);
            // As AnimationSystem: holding its pose for its owner's death, it takes it when heard.
            auto& staged = actor.Replay;
            if (uint8_t waiting = AnimationReplayStep::kClips; engine.PoseHeld(form.Id) && staged.Phase.compare_exchange_strong(waiting, AnimationReplayStep::kIdle))
                animation.StagedUpdates = 0;
            const bool cStepFree = streams.OnStream(form.Id) || staged.Phase.load() == AnimationReplayStep::kIdle;
            bool deathNow = cStepFree && ::ReplayState::TakeOverdueDeath(animation.TimePoints, engine.PoseHeld(form.Id),
                                                                         [](const ActionEvent& acAction) { return DeathActions::IsDying(acAction.State1); });
            if (streams.OnStream(form.Id))
            {
                while (!animation.TimePoints.empty() && ::ReplayState::ActionDue(animation.TimePoints.front().Tick, cPlayback, deathNow))
                {
                    const ActionEvent cAction = animation.TimePoints.front();
                    // Its death on its stream: the kill beside the graph, its death's clip its stream's (AnimationSystem).
                    if (DeathActions::IsDying(cAction.State1) && !actor.Dead)
                        engine.KillBesideGraph(form.Id, cAction);
                    // Its owner's pair: its half by this action, unless its partner here is a copy too (AnimationSystem).
                    const uint32_t cPartnerId = cAction.TargetActor ? Utils::GetFormIdBySessionId(cAction.TargetActor - 1) : cAction.TargetId;
                    const auto cPartner = engine.Actors.find(cPartnerId);
                    const bool cKillMove = !cAction.TargetEventName.empty() && ::ReplayState::EntersKillMove(cAction.EventName);
                    const bool cHasPartner = cPartner != engine.Actors.end();
                    const auto cClaim = cKillMove ? aWorld.GetActionClaims().Of(Utils::GetSessionIdByFormId(form.Id).value_or(0), cAction.Tick)
                                                  : ::ReplayState::ClaimAnswer::kNotNeeded;
                    // As AnimationSystem: refused, skipped; on an NPC still this game's, it waits for the host's answer.
                    const auto cClaimOutcome = ::ReplayState::ClaimOutcome(
                        cClaim, cHasPartner && ::ReplayState::ClaimedAway(cKillMove, cPartner->second.Copy, cPartner->second.Player));
                    if (cClaimOutcome == ::ReplayState::PairOutcome::kWait)
                        break;
                    if (cClaimOutcome == ::ReplayState::PairOutcome::kSkip)
                    {
                        animation.TimePoints.pop_front();
                        animation.LastRanAction = cAction;
                        engine.SetActorLatestAnimation(form.Id, cAction);
                        continue;
                    }
                    const bool cPartnerCopy = cHasPartner && cPartner->second.Copy;
                    const bool cTaken = cHasPartner && ::ReplayState::TakenVictim(cKillMove, cClaim, cPartnerCopy, cPartner->second.Player);
                    if (!cAction.TargetEventName.empty() && !::ReplayState::PairOnStream(cPartnerCopy, cTaken))
                    {
                        streams.HoldUntilPoint(form.Id, cAction.Tick);
                        break;
                    }
                    // What playing the action does beside the graph (39004), as AnimationSystem.
                    if (cKillMove)
                    {
                        actor.InKillMove = cPartner->second.InKillMove = true;
                        cPartner->second.Killer = form.Id;
                    }
                    animation.TimePoints.pop_front();
                    // Its owner's get-up stands its own body up; its graph's get-up is the stream's (AnimationSystem).
                    if (KnockReplay::StandsUp(actor.KnockedDown ? 1 : 0, cAction.ActionId == FakeEngine::kActionGetUp))
                        engine.StandUp(form.Id);
                    if (!actor.Dead && KnockReplay::GoesDown(engine.KnockStateOf(form.Id), cAction.ActionId == FakeEngine::kActionRagdoll))
                        aWorld.ctx().at<BodyService>().KnockDown(form.Id);
                    actor.State1 = ActorStateRules::FromSnapshot1(cAction.StateAfter1, actor.State1); // as AnimationSystem
                    animation.LastRanAction = cAction;
                    engine.SetActorLatestAnimation(form.Id, cAction);
                }
                continue;
            }
            if (actor.PairStops)
            {
                --actor.PairStops;
                if (::ReplayState::ResendPairStop(1, actor.InKillMove, actor.InPairClip))
                    engine.StopPairClip(form.Id);
                if (engine.Actors.contains(actor.ClashPartner) &&
                    ::ReplayState::ResendPairStop(1, engine.Actors.at(actor.ClashPartner).InKillMove, engine.Actors.at(actor.ClashPartner).InPairClip))
                {
                    engine.StopPairClip(actor.ClashPartner);
                    if (actor.PairStops)
                        continue;
                }
            }
            auto& step = actor.Replay;
            using Step = AnimationReplayStep;
            const uint8_t cPhase = step.Phase.load();
            if (cPhase == Step::kTaken || cPhase == Step::kRefused)
            {
                    if (cPhase == Step::kTaken)
                {
                    animation.LastRanAction = step.Action;
                    engine.SetActorLatestAnimation(form.Id, step.Action);
                }
                if (!animation.TimePoints.empty() && animation.TimePoints.front().Tick == step.Action.Tick &&
                    animation.TimePoints.front().ActionId == step.Action.ActionId)
                    animation.TimePoints.pop_front();
                step.Phase.store(Step::kIdle);
                continue;
            }
            if (cPhase == Step::kClips)
            {
                if (ClipWaitOver(step, ++animation.StagedUpdates))
                {
                    animation.StagedUpdates = 0;
                    step.Phase.store(Step::kVariables);
                }
                continue;
            }
            if (cPhase != Step::kIdle)
            {
                // Native Update falls back after ten main-thread updates if no graph job takes the step.
                if (++animation.StagedUpdates > 10 && cPhase != Step::kBusy)
                {
                    step.Phase.store(Step::kBusy);
                    engine.ActorLoadAnimationVariables(form.Id, step.Variables);
                    step.Phase.store(engine.PlayReplayed(form.Id, step.Action) ? Step::kTaken : Step::kRefused);
                }
                continue;
            }
            // As AnimationSystem: held for its owner's pair, it may ask once that action has played.
            if (streams.Held(form.Id) && std::ranges::none_of(animation.TimePoints, [&](const ActionEvent& acAction) {
                    return !acAction.TargetEventName.empty() && acAction.Tick <= cPlayback;
                }))
                streams.PairPlayed(form.Id);
            if (animation.TimePoints.empty()) continue;
            const auto& action = animation.TimePoints.front();
            if (!::ReplayState::ActionDue(action.Tick, InterpolationSystem::PlaybackTick(aWorld, cEntity), deathNow)) continue;
            if (!engine.IsAnimationGraphReady(form.Id)) continue;
            if (DeathActions::IsDying(action.State1))
            {
                if (engine.HoldDeathActions) continue;
                engine.ReplayDeathAction(form.Id, action);
                animation.TimePoints.pop_front();
                continue;
            }
            if (actor.GraphState == "dead" || actor.GraphState == "recovering")
            {
                animation.TimePoints.pop_front();
                continue;
            }
            ActionEvent replayed = action;
            if (replayed.TargetActor)
                replayed.TargetId = Utils::GetFormIdBySessionId(replayed.TargetActor - 1);
            const auto cPartner = engine.Actors.find(replayed.TargetId);
            const bool cKillMove = ::ReplayState::EntersKillMove(replayed.EventName);
            // As AnimationSystem: refused, skipped; on an NPC still this game's, it waits for the host's answer.
            const bool cHasPartner = !replayed.TargetEventName.empty() && cPartner != engine.Actors.end();
            const auto cClaim = cKillMove ? aWorld.GetActionClaims().Of(Utils::GetSessionIdByFormId(form.Id).value_or(0), replayed.Tick)
                                          : ::ReplayState::ClaimAnswer::kNotNeeded;
            const auto cClaimOutcome = ::ReplayState::ClaimOutcome(
                cClaim, cHasPartner && ::ReplayState::ClaimedAway(cKillMove, cPartner->second.Copy, cPartner->second.Player));
            if (cClaimOutcome == ::ReplayState::PairOutcome::kWait)
                continue;
            if (cClaimOutcome == ::ReplayState::PairOutcome::kSkip)
            {
                animation.LastRanAction = replayed;
                engine.SetActorLatestAnimation(form.Id, replayed);
                animation.TimePoints.pop_front();
                continue;
            }
            const bool cTaken = cPartner != engine.Actors.end() && ::ReplayState::TakenVictim(cKillMove, cClaim, cPartner->second.Copy, cPartner->second.Player);
            const bool cLocalPartner = cHasPartner &&
                                       ::ReplayState::PairsHere(cPartner->second.Copy, engine.IsActorMount(replayed.TargetId), cPartner->second.Player, cKillMove, cTaken);
            step.PairHere = cLocalPartner;
            if (cLocalPartner && !cTaken)
            {
                const auto cOutcome = ::ReplayState::SettlePairedReplay(cPartner->second.InKillMove, cClaim);
                if (cOutcome == ::ReplayState::PairOutcome::kWait)
                    continue;
                if (cOutcome == ::ReplayState::PairOutcome::kYield)
                {
                    engine.ActorYieldPair(replayed.TargetId, form.Id);
                    continue;
                }
                if (cOutcome == ::ReplayState::PairOutcome::kSkip)
                {
                    animation.LastRanAction = replayed;
                    engine.SetActorLatestAnimation(form.Id, replayed);
                    animation.TimePoints.pop_front();
                    continue;
                }
            }
            step.Action = replayed;
            if (action.Tick >= animation.VariablesTick)
            {
                animation.Variables = action.Variables;
                animation.VariablesTick = action.Tick;
            }
            step.Variables = animation.Variables;
            step.Retries = 0;
            animation.StagedUpdates = 0;
            step.ClipsLoaded = engine.RequestActionClips(form.Id, action.EventName);
            step.Phase.store(step.ClipsLoaded ? Step::kClips : Step::kVariables);
        }
        // The same graph-job phase machine as Skyrim, with the fake's graph behind it.
        for (auto&& [cEntity, animation, form] : aWorld.view<RemoteAnimationComponent, FormIdComponent>().each())
            if (engine.IsAnimationGraphReady(form.Id) && engine.Actors.at(form.Id).GraphUpdating && !aWorld.all_of<WaitingFor3D>(cEntity))
            {
                auto& step = engine.Actors.at(form.Id).Replay;
                RunAnimationReplayStep(step, [&](const AnimationVariables& acVariables) {
                    engine.ActorLoadAnimationVariables(form.Id, acVariables);
                }, [&] {
                    if (step.Action.Variables.GraphRandom)
                        engine.Actors.at(form.Id).GraphRandom = step.Action.Variables.GraphRandom;
                    return engine.PlayReplayed(form.Id, step.Action);
                });
            }
        auto& waitEngine = aWorld.ctx().at<FakeEngine&>();
        const float cTimeScale = waitEngine.Calendar.TimeScale;
        if (aWorld.ctx().at<FakeWait&>().Open)
            waitEngine.Calendar.TimeScale = 0.f;
        waitEngine.Frame();
        waitEngine.Calendar.TimeScale = cTimeScale;
    }

private:
    World* m_pWorld{};
    FakeEngine m_engine;
    FakeDialogue m_dialogue;
    FakeSteam m_steam;
    FakeWait m_wait{m_engine};
    FakeCrime m_crime{m_engine};
    FakeBugReports m_bugReports;
    FakeDevTools m_devTools{m_engine};
    std::chrono::high_resolution_clock::time_point m_lastFrameTime{std::chrono::high_resolution_clock::now()};
};
} // namespace

std::unique_ptr<IEngine> MakeFakeEngine()
{
    return std::make_unique<FakeEngineSet>();
}

void SetCurrentWorld(World* apWorld) noexcept
{
    World::SetCurrent(apWorld);
}

// Skyrim 1.6.1170 with SKSE 2.2.6; Skyrim.esm, Update.esm, a light ccBGSSSE001-Fish.esm (HostAuthorityTests' host).
namespace RunningGame
{
const GameVersions& GetVersions()
{
    static const GameVersions s_versions{GameVersions::Pack(1, 6, 1170), GameVersions::Pack(2, 2, 6)};
    return s_versions;
}

// As RunningGame::Plugins: the loaded plugins the bridge lists.
Mods Plugins(const IFormBridge& acForms)
{
    Mods plugins;
    for (const auto& cMod : acForms.GetLoadedMods())
        plugins.ModList.push_back({cMod.Filename, cMod.Id, cMod.IsLite});
    return plugins;
}
} // namespace RunningGame

// Each real game has its own main-loop queue. The fake runs it before starting that game's jobs.
namespace MainLoopTasks
{
void Queue(std::function<void()> aTask) noexcept
{
    World::Get().ctx().at<FakeEngine&>().MainTasks.push_back(std::move(aTask));
}
void Run() noexcept
{
    auto tasks = std::move(World::Get().ctx().at<FakeEngine&>().MainTasks);
    World::Get().ctx().at<FakeEngine&>().MainTasks.clear();
    for (auto& task : tasks) task();
}
} // namespace MainLoopTasks
