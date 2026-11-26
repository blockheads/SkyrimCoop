#include <core/World.h>

#include <core/IEngine.h>
#include <core/CreatedIds.h>
#include <ports/IActorBridge.h>
#include <physics/IBodyBridge.h>
#include <tools/IDevToolsBridge.h>
#include <animation/IAnimationBody.h>
#include <ports/IFormBridge.h>
#include <ports/IGuestEngine.h>
#include <ports/IHostEngine.h>
#include <join/IJoinBridge.h>
#include <ports/IPlayerBridge.h>
#include <ports/IWorldBridge.h>
#include <animation/ActionClaims.h>
#include <animation/GraphStream.h>
#include <core/TransportService.h>
#include <core/RunnerService.h>
#include <world/ObjectService.h>
#include <actorstate/ActorValueService.h>
#include <items/InventoryService.h>
#include <magic/EffectVisualsService.h>
#include <scripts/CutsceneService.h>
#include <scripts/SystemQuestService.h>
#include <actorstate/AiStateService.h>
#include <actorstate/ActorStateService.h>
#include <characters/ClaimService.h>
#include <combat/KillMoveService.h>
#include <core/CopyRecords.h>
#include <core/CopyRecordsService.h>
#include <actorstate/PerkService.h>
#include <dialogue/DialogueService.h>
#include <physics/BodyService.h>
#include <tools/BugReportService.h>
#include <crime/CrimeService.h>
#include <time/WaitService.h>
#include <join/JoinService.h>
#include <join/SaveService.h>
#include <followers/FollowerService.h>
#include <join/SteamService.h>
#include <magic/MagicService.h>
#include <time/CommandService.h>
#include <tools/DevToolsService.h>
#include <time/TimeScaleService.h>
#include <core/PlayerService.h>
#include <death/DeathService.h>
#include <combat/CombatService.h>
#include <game/GuestService.h>
#include <travel/MapService.h>
#include <travel/PartyMarkerService.h>
#include <game/HostService.h>
#include <game/SessionEnd.h>
#include <core/SessionService.h>
#include <characters/CharacterService.h>
#include <travel/TravelService.h>
#include <core/UpdateEvent.h>

namespace
{
World* s_pCurrent = nullptr;

// The ports the services look up, before the first of them (TransportService) is made. Each by its own interface:
// with multiple inheritance, entt's ctx().emplace<T&> keeps the pointer as given.
std::unique_ptr<IEngine> Registered(World& aWorld, std::unique_ptr<IEngine> apEngine)
{
    aWorld.ctx().emplace<IActorBridge&>(apEngine->Actors());
    aWorld.ctx().emplace<IFormBridge&>(apEngine->Forms());
    aWorld.ctx().emplace<IPlayerBridge&>(apEngine->Player());
    aWorld.ctx().emplace<IWorldBridge&>(apEngine->WorldBridge());
    aWorld.ctx().emplace<IBodyBridge&>(apEngine->Bodies());
    aWorld.ctx().emplace<IJoinBridge&>(apEngine->Join());
    aWorld.ctx().emplace<IDevToolsBridge&>(apEngine->DevTools());
    aWorld.ctx().emplace<IAnimationBody&>(apEngine->AnimationBodies());
    return apEngine;
}
} // namespace

World::World(std::unique_ptr<IEngine> apEngine)
    : m_pEngine(Registered(*this, std::move(apEngine)))
    , m_pGraphStreams(std::make_unique<GraphStreams>())
    , m_pActionClaims(std::make_unique<ActionClaims>())
    , m_runner(m_dispatcher)
    , m_transport(*this, m_dispatcher)
{
    m_pEngine->AddFirstServices(*this);
    // Before CharacterService: an arrival drops the copies the old cell took before they are checked.
    ctx().emplace<TravelService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<CreatedIds>();
    ctx().emplace<CopyRecords>(); // before CharacterService: its Ride part
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport,
                                   ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(), ctx().at<IPlayerBridge&>());
    m_pEngine->AddServicesAfterCharacters(*this);
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport,
                                  ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>());
    ctx().emplace<TimeScaleService>(*this, m_dispatcher, m_transport);
    ctx().emplace<WaitService>(*this, m_dispatcher, m_transport, m_pEngine->Wait());
    ctx().emplace<BodyService>(*this, m_dispatcher, m_pEngine->Bodies());
    ctx().emplace<CrimeService>(*this, m_dispatcher, m_transport, m_pEngine->Crime());
    ctx().emplace<SessionService>(*this, m_dispatcher);
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport,
                               ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<EffectVisualsService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<PerkService>(m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<CopyRecordsService>(*this, m_dispatcher, m_transport, ctx().at<CopyRecords>());
    ctx().emplace<AiStateService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<CopyRecords>());
    ctx().emplace<ActorStateService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<CopyRecords>());
    ctx().emplace<ClaimService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<KillMoveService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                   ctx().at<CopyRecords>());
    ctx().emplace<CutsceneService>(*this, m_dispatcher, m_transport);
    ctx().emplace<SystemQuestService>(*this, m_dispatcher, m_transport);
    ctx().emplace<BugReportService>(*this, m_dispatcher, m_transport, m_pEngine->BugReports());
    ctx().emplace<DialogueService>(*this, m_dispatcher, m_transport, m_pEngine->Dialogue());
    ctx().emplace<CommandService>(m_transport, m_dispatcher);
    ctx().emplace<DevToolsService>(*this, m_dispatcher, m_transport, m_pEngine->DevTools());
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport, ctx().at<IPlayerBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<DeathService>(*this, m_dispatcher, m_transport, ctx().at<IPlayerBridge&>(), ctx().at<IActorBridge&>());
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher, ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<MapService>(*this, m_dispatcher, m_transport);
    ctx().emplace<PartyMarkerService>(*this, m_dispatcher, ctx().at<IActorBridge&>(), ctx().at<IPlayerBridge&>());
    ctx().emplace<HostService>(*this, m_dispatcher, [this] { return m_pEngine->MakeHostEngine(); });
    ctx().emplace<GuestService>(*this, m_dispatcher, [this] { return m_pEngine->MakeGuestEngine(); });
    ctx().emplace<SteamService>(*this, m_dispatcher, m_pEngine->Steam());
    ctx().emplace<JoinService>(*this, m_dispatcher, m_pEngine->Join());
    ctx().emplace<SaveService>(*this, m_dispatcher);
    WireSessionEnd(*this);
    ctx().emplace<FollowerService>(*this, m_dispatcher, ctx().at<IPlayerBridge&>());
    m_pEngine->Started(*this);
}

World::~World()
{
    // The services live in the registry's context, which is destroyed after our own members: their
    // connections to m_dispatcher would be released against a dispatcher already gone (it crashed
    // hot reloads now and then). They go first, newest first.
    m_pEngine->Stopping(*this);
    ctx().erase<FollowerService>();
    ctx().erase<SaveService>();
    ctx().erase<JoinService>();
    ctx().erase<SteamService>();
    ctx().erase<GuestService>();
    ctx().erase<HostService>();
    ctx().erase<PartyMarkerService>();
    ctx().erase<MapService>();
    ctx().erase<CombatService>();
    ctx().erase<DeathService>();
    ctx().erase<PlayerService>();
    ctx().erase<DevToolsService>();
    ctx().erase<CommandService>();
    ctx().erase<DialogueService>();
    ctx().erase<BugReportService>();
    ctx().erase<SystemQuestService>();
    ctx().erase<CutsceneService>();
    ctx().erase<PerkService>();
    ctx().erase<InventoryService>();
    ctx().erase<KillMoveService>();
    ctx().erase<ClaimService>();
    ctx().erase<ActorStateService>();
    ctx().erase<AiStateService>();
    ctx().erase<CopyRecordsService>();
    ctx().erase<EffectVisualsService>();
    ctx().erase<MagicService>();
    ctx().erase<ActorValueService>();
    ctx().erase<SessionService>();
    ctx().erase<BodyService>();
    ctx().erase<WaitService>();
    ctx().erase<CrimeService>();
    ctx().erase<TimeScaleService>();
    ctx().erase<ObjectService>();
    m_pEngine->RemoveServicesAfterCharacters(*this);
    ctx().erase<CharacterService>();
    ctx().erase<CopyRecords>();
    ctx().erase<TravelService>();
    m_pEngine->RemoveFirstServices(*this);
    if (s_pCurrent == this)
        s_pCurrent = nullptr;
}

void World::Update() noexcept
{
    m_pEngine->Frame(*this);
}

void World::Step(double aDeltaSeconds, double aEngineFrameMs) noexcept
{
    m_frameClock.Advance(GetTick(), aEngineFrameMs);
    m_copyClock.Advance(m_frameClock.Now(), aEngineFrameMs);
    m_pGraphStreams->SetTick(m_frameClock.Now());
    // Force run this before so we get the tasks scheduled to run
    m_runner.OnUpdate(UpdateEvent(aDeltaSeconds));
    m_dispatcher.trigger(UpdateEvent(aDeltaSeconds));
}

World& World::Get() noexcept
{
    return *s_pCurrent;
}

bool World::Exists() noexcept
{
    return s_pCurrent != nullptr;
}

void World::SetCurrent(World* apWorld) noexcept
{
    s_pCurrent = apWorld;
}

RunnerService& World::GetRunner() noexcept
{
    return m_runner;
}

TransportService& World::GetTransport() noexcept
{
    return m_transport;
}

IActorBridge& World::GetActorBridge() noexcept
{
    return ctx().at<IActorBridge&>();
}

IFormBridge& World::GetFormBridge() noexcept
{
    return ctx().at<IFormBridge&>();
}

IPlayerBridge& World::GetPlayerBridge() noexcept
{
    return ctx().at<IPlayerBridge&>();
}

IWorldBridge& World::GetWorldBridge() noexcept
{
    return ctx().at<IWorldBridge&>();
}

uint64_t World::GetTick() const noexcept
{
    return m_transport.GetTick();
}

SessionService& World::GetSessionService() noexcept
{
    return ctx().at<SessionService>();
}

const SessionService& World::GetSessionService() const noexcept
{
    return ctx().at<const SessionService>();
}

TravelService& World::GetTravelService() noexcept
{
    return ctx().at<TravelService>();
}

const TravelService& World::GetTravelService() const noexcept
{
    return ctx().at<const TravelService>();
}

CharacterService& World::GetCharacterService() noexcept
{
    return ctx().at<CharacterService>();
}

const CharacterService& World::GetCharacterService() const noexcept
{
    return ctx().at<const CharacterService>();
}

MagicService& World::GetMagicService() noexcept
{
    return ctx().at<MagicService>();
}

const MagicService& World::GetMagicService() const noexcept
{
    return ctx().at<const MagicService>();
}

Hosting& World::GetHosting() noexcept
{
    return GetHostService();
}

HostService& World::GetHostService() noexcept
{
    return ctx().at<HostService>();
}

const HostService& World::GetHostService() const noexcept
{
    return ctx().at<const HostService>();
}
