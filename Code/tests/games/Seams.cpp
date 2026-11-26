// What the real client links from engine files, for games in this process: World (only the services a flow needs, on
// the fake engine) and RunningGame (the host's game; the plugins the fake engine lists).
#include <core/CreatedIds.h>
#include <animation/ReplayState.h>
#include <animation/KnockReplay.h>
#include <core/SessionIds.h>
#include "FakeDialogue.h"
#include <core/MainLoopTasks.h>
#include "FakeWait.h"
#include "FakeCrime.h"
#include "FakeBugReports.h"
#include <crime/CrimeService.h>
#include <time/WaitService.h>
#include <physics/BodyService.h>
#include "FakeSteam.h"
#include "FakeEngine.h"
#include <actorstate/DeathActions.h>

#include <ports/IActorBridge.h>
#include "FakeGuestEngine.h"
#include <core/IEngine.h>
#include "FakeHostEngine.h"
#include <game/GuestService.h>

#include <animation/ActionClaims.h>
#include <animation/GraphStream.h>
#include <core/UpdateEvent.h>
#include <core/RunningGame.h>
#include <animation/RemoteAnimationComponent.h>
#include <core/FormIdComponent.h>
#include <animation/SpawnGraphStateComponent.h>
#include <characters/WaitingFor3D.h>
#include <movement/InterpolationSystem.h>
#include <actorstate/ActorValueService.h>
#include <actorstate/AiStateService.h>
#include <actorstate/ActorStateService.h>
#include <characters/ClaimService.h>
#include <combat/KillMoveService.h>
#include <animation/ActorStateRules.h>
#include <core/CopyRecords.h>
#include <core/CopyRecordsService.h>
#include <actorstate/PerkService.h>
#include <magic/EffectVisualsService.h>
#include <characters/CharacterService.h>
#include <combat/CombatService.h>
#include <scripts/CutsceneService.h>
#include <scripts/SystemQuestService.h>
#include <tools/BugReportService.h>
#include <tools/DevToolsService.h>
#include "FakeAnimationBody.h"
#include "FakeDevTools.h"
#include <dialogue/DialogueService.h>
#include <game/HostService.h>
#include <items/InventoryService.h>
#include <magic/MagicService.h>
#include <travel/MapService.h>
#include <travel/PartyMarkerService.h>
#include <world/ObjectService.h>
#include <time/TimeScaleService.h>
#include <core/PlayerService.h>
#include <death/DeathService.h>
#include <travel/TravelService.h>
#include <core/RunnerService.h>
#include <join/JoinService.h>
#include <join/SaveService.h>
#include <followers/FollowerService.h>
#include <game/SessionEnd.h>
#include <core/SessionService.h>
#include <join/SteamService.h>
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>
#include <core/World.h>

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
    IAnimationBody& AnimationBodies() noexcept override { return m_animationBodies; }
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
        auto& engine = aWorld.ctx().at<FakeEngine&>();
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
    FakeAnimationBody m_animationBodies{m_engine};
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
