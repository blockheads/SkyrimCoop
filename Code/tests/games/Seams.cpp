// What the real client links from engine files, for games in this process: World (only the services a flow needs, on
// the fake engine) and RunningGame (the host's game; the plugins the fake engine lists).
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
#include "GuestSeams.h"

#include <Events/UpdateEvent.h>
#include <RunningGame.h>
#include <Components/RemoteAnimationComponent.h>
#include <Components/FormIdComponent.h>
#include <Components/SpawnGraphStateComponent.h>
#include <Components/WaitingFor3D.h>
#include <Systems/InterpolationSystem.h>
#include <Services/ActorValueService.h>
#include <Services/AiStateService.h>
#include <Services/PerkService.h>
#include <Services/EffectVisualsService.h>
#include <Services/CharacterService.h>
#include <Services/CombatService.h>
#include <Services/CutsceneService.h>
#include <Services/SystemQuestService.h>
#include <Services/BugReportService.h>
#include <Services/DialogueService.h>
#include <Services/HostService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/MapService.h>
#include <Services/ObjectService.h>
#include <Services/TimeScaleService.h>
#include <Services/PlayerService.h>
#include <Services/DeathService.h>
#include <Services/TravelService.h>
#include <Services/RunnerService.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Services/FollowerService.h>
#include <Services/SessionService.h>
#include <Services/SteamService.h>
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>
#include <World.h>

// The engine behind the bridges: the fake.
class SkyrimBridge : public FakeEngine
{
};

namespace
{
World* s_pCurrent = nullptr;

std::unique_ptr<SkyrimBridge> CreateBridgeContext(World& aWorld)
{
    auto bridge = std::make_unique<SkyrimBridge>();
    FakeEngine& engine = *bridge;
    aWorld.ctx().emplace<FakeEngine&>(engine);
    aWorld.ctx().emplace<IActorBridge&>(static_cast<IActorBridge&>(engine));
    aWorld.ctx().emplace<IFormBridge&>(static_cast<IFormBridge&>(engine));
    aWorld.ctx().emplace<IPlayerBridge&>(static_cast<IPlayerBridge&>(engine));
    aWorld.ctx().emplace<IWorldBridge&>(static_cast<IWorldBridge&>(engine));
    aWorld.ctx().emplace<IJoinBridge&>(static_cast<IJoinBridge&>(engine));
    aWorld.ctx().emplace<IBodyBridge&>(static_cast<IBodyBridge&>(engine));
    return bridge;
}
} // namespace

void SetCurrentWorld(World* apWorld) noexcept
{
    s_pCurrent = apWorld;
}

World::World()
    : m_pSkyrimBridge(CreateBridgeContext(*this)), m_pDialogueBridge(std::make_unique<FakeDialogue>()),
      m_pSteamBridge(std::make_unique<FakeSteam>()),
      m_pWaitBridge(std::make_unique<FakeWait>(ctx().at<FakeEngine&>())),
      m_pCrimeBridge(std::make_unique<FakeCrime>()), m_pBugReportBridge(std::make_unique<FakeBugReports>()), m_runner(m_dispatcher), m_transport(*this, m_dispatcher)
{
    m_lastFrameTime = std::chrono::high_resolution_clock::now();
    m_dispatcher.sink<UpdateEvent>().connect<&RunnerService::OnUpdate>(m_runner);
    // In the real World's order: TravelService before CharacterService.
    ctx().emplace<TravelService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                    ctx().at<IPlayerBridge&>());
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport, ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>());
    ctx().emplace<TimeScaleService>(*this, m_dispatcher, m_transport);
    ctx().emplace<FakeWait&>(static_cast<FakeWait&>(*m_pWaitBridge));
    ctx().emplace<WaitService>(*this, m_dispatcher, m_transport, *m_pWaitBridge);
    ctx().emplace<BodyService>(*this, m_dispatcher, ctx().at<IBodyBridge&>());
    ctx().emplace<FakeCrime&>(static_cast<FakeCrime&>(*m_pCrimeBridge));
    ctx().emplace<CrimeService>(*this, m_dispatcher, m_transport, *m_pCrimeBridge);
    ctx().emplace<SessionService>(*this, m_dispatcher);
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<EffectVisualsService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<PerkService>(m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<AiStateService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<CutsceneService>(*this, m_dispatcher, m_transport);
    ctx().emplace<SystemQuestService>(*this, m_dispatcher, m_transport);
    ctx().emplace<FakeBugReports&>(static_cast<FakeBugReports&>(*m_pBugReportBridge));
    ctx().emplace<BugReportService>(*this, m_dispatcher, m_transport, *m_pBugReportBridge);
    ctx().emplace<FakeDialogue&>(static_cast<FakeDialogue&>(*m_pDialogueBridge));
    // An NPC's AI sets its slot; a copy runs none (Skyrim: the copy's slots stayed empty).
    static_cast<FakeDialogue&>(*m_pDialogueBridge).SpeakerLooks = [](uint32_t aSpeaker, uint32_t aTarget) {
        FakeEngine& engine = FakeEngine::Current();
        if (const auto it = engine.Actors.find(aSpeaker); it != engine.Actors.end() && !it->second.Copy)
            engine.LooksAt(aSpeaker, FakeEngine::kDialogueHeadTrack, aTarget);
    };
    static_cast<FakeDialogue&>(*m_pDialogueBridge).DialogueLoaded = [](uint32_t aQuest) {
        return FakeEngine::Current().DialogueLoaded.contains(aQuest);
    };
    ctx().emplace<DialogueService>(*this, m_dispatcher, m_transport, *m_pDialogueBridge);
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport, ctx().at<IPlayerBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<DeathService>(*this, m_dispatcher, m_transport, ctx().at<IPlayerBridge&>(), ctx().at<IActorBridge&>());
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher, ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<MapService>(*this, m_dispatcher, m_transport);
    ctx().emplace<HostService>(*this, m_dispatcher);
    ctx().emplace<GuestSeam>(*this);
    ctx().emplace<FakeSteam&>(static_cast<FakeSteam&>(*m_pSteamBridge));
    ctx().emplace<SteamService>(*this, m_dispatcher, *m_pSteamBridge);
    ctx().emplace<JoinService>(*this, m_dispatcher, ctx().at<IJoinBridge&>());
    ctx().emplace<SaveService>(*this, m_dispatcher);
    ctx().emplace<FollowerService>(*this, m_dispatcher, ctx().at<IPlayerBridge&>());
}

// As the real World: the services go first, newest first, while the dispatcher and the engine they use still live.
World::~World()
{
    ctx().erase<FollowerService>();
    ctx().erase<SaveService>();
    ctx().erase<JoinService>();
    ctx().erase<SteamService>();
    ctx().erase<GuestSeam>();
    ctx().erase<HostService>();
    ctx().erase<MapService>();
    ctx().erase<CombatService>();
    ctx().erase<DeathService>();
    ctx().erase<PlayerService>();
    ctx().erase<DialogueService>();
    ctx().erase<BugReportService>();
    ctx().erase<SystemQuestService>();
    ctx().erase<CutsceneService>();
    ctx().erase<PerkService>();
    ctx().erase<AiStateService>();
    ctx().erase<EffectVisualsService>();
    ctx().erase<MagicService>();
    ctx().erase<InventoryService>();
    ctx().erase<ActorValueService>();
    ctx().erase<SessionService>();
    ctx().erase<BodyService>();
    ctx().erase<WaitService>();
    ctx().erase<CrimeService>();
    ctx().erase<TimeScaleService>();
    ctx().erase<ObjectService>();
    ctx().erase<CharacterService>();
    ctx().erase<TravelService>();
}

World& World::Get() noexcept
{
    return *s_pCurrent;
}

void World::Update() noexcept
{
    auto& mainEngine = ctx().at<FakeEngine&>();
    mainEngine.AtMainLoop = true;
    MainLoopTasks::Run();
    mainEngine.AtMainLoop = false;
    ctx().at<FakeEngine&>().RunMainLoop();
    const auto cNow = std::chrono::high_resolution_clock::now();
    const double cDelta = std::chrono::duration<double>(cNow - m_lastFrameTime).count();
    m_lastFrameTime = cNow;
    m_copyClock.Advance(GetTick());
    // A delayed line delivers in wall time: recovery must use that clock too.
    m_dispatcher.trigger(UpdateEvent(std::getenv("COOP_NET") ? cDelta : 1.0 / 60));
    // The real AnimationSystem replays a dying owner's action at its copy's time (CopyPlayback).
    auto& engine = ctx().at<FakeEngine&>();
    for (auto&& [cEntity, animation, form] : view<RemoteAnimationComponent, FormIdComponent>().each())
    {
        if (!engine.ActorExists(form.Id)) continue;
        if (auto* spawn = try_get<SpawnGraphStateComponent>(cEntity))
        {
            if (all_of<WaitingFor3D>(cEntity) || !engine.IsAnimationGraphReady(form.Id)) continue;
            if (engine.LoadActorGraphState(form.Id, spawn->State) && !animation.TimePoints.empty())
            {
                animation.LastRanAction = animation.TimePoints.front();
                animation.Variables = animation.LastRanAction.Variables;
                animation.VariablesTick = animation.LastRanAction.Tick;
                engine.SetActorLatestAnimation(form.Id, animation.TimePoints.front());
                animation.TimePoints.pop_front();
            }
            remove<SpawnGraphStateComponent>(cEntity);
        }
        auto& actor = engine.Actors.at(form.Id);
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
        if (cPhase != Step::kIdle)
        {
            // Native Update falls back after ten main-thread updates if no graph job takes the step.
            if (++animation.StagedUpdates > 10 && cPhase != Step::kBusy)
            {
                step.Phase.store(Step::kBusy);
                engine.ActorLoadAnimationVariables(form.Id, step.Variables);
                step.Phase.store(engine.ReplayAnimation(form.Id, step.Action) ? Step::kTaken : Step::kRefused);
            }
            continue;
        }
        if (animation.TimePoints.empty()) continue;
        const auto& action = animation.TimePoints.front();
        if (InterpolationSystem::PlaybackTick(*this, cEntity) < action.Tick) continue;
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
        step.Action = action;
        if (action.Tick >= animation.VariablesTick)
        {
            animation.Variables = action.Variables;
            animation.VariablesTick = action.Tick;
        }
        step.Variables = animation.Variables;
        step.Retries = 0;
        animation.StagedUpdates = 0;
        step.Phase.store(Step::kVariables);
    }
    // The same graph-job phase machine as Skyrim, with the fake's graph behind it.
    for (auto&& [cEntity, animation, form] : view<RemoteAnimationComponent, FormIdComponent>().each())
        if (engine.IsAnimationGraphReady(form.Id) && engine.Actors.at(form.Id).GraphUpdating && !all_of<WaitingFor3D>(cEntity))
        {
            auto& step = engine.Actors.at(form.Id).Replay;
            RunAnimationReplayStep(step, [&](const AnimationVariables& acVariables) {
                engine.ActorLoadAnimationVariables(form.Id, acVariables);
            }, [&] { return engine.ReplayAnimation(form.Id, step.Action); });
        }
    ctx().at<GuestSeam>().Update();
    auto& waitEngine = ctx().at<FakeEngine&>();
    const float cTimeScale = waitEngine.Calendar.TimeScale;
    if (ctx().at<FakeWait&>().Open)
        waitEngine.Calendar.TimeScale = 0.f;
    waitEngine.Frame();
    waitEngine.Calendar.TimeScale = cTimeScale;
}

RunnerService& World::GetRunner() noexcept
{
    return m_runner;
}

IActorBridge& World::GetActorBridge() noexcept
{
    return ctx().at<IActorBridge&>();
}

// As the real World: the session's clock (the host's network's, or the guest's estimate of it).
uint64_t World::GetTick() const noexcept
{
    return m_transport.GetTick();
}

TransportService& World::GetTransport() noexcept
{
    return m_transport;
}

IFormBridge& World::GetFormBridge() noexcept
{
    return ctx().at<IFormBridge&>();
}

TravelService& World::GetTravelService() noexcept
{
    return ctx().at<TravelService>();
}

CharacterService& World::GetCharacterService() noexcept
{
    return ctx().at<CharacterService>();
}

HostService& World::GetHostService() noexcept
{
    return ctx().at<HostService>();
}

SessionService& World::GetSessionService() noexcept
{
    return ctx().at<SessionService>();
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
