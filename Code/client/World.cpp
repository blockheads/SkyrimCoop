#include "World.h"

// SkyrimBridge is included here (not in World.h) to keep SKSE headers out of
// every service that includes World.h.
#include <Bridges/SkyrimBridge.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Bridges/IWorldBridge.h>

#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Services/ImguiService.h>
#include <Ui/UiService.h>
#include <Services/PapyrusService.h>
#include <Services/ObjectService.h>
#include <Services/ActorValueService.h>
#include <Services/InventoryService.h>
#include <Services/EffectVisualsService.h>
#include <Services/CutsceneService.h>
#include <Services/AiStateService.h>
#include <Services/DialogueService.h>
#include <Bridges/DialogueBridge.h>
#include <Services/MagicService.h>
#include <Services/CommandService.h>
#include <Services/TimeScaleService.h>
#include <Services/PlayerService.h>
#include <Services/CombatService.h>
#include <Services/GuestService.h>
#include <Services/MapService.h>
#include <Services/HostService.h>
#include <Services/SessionService.h>
#include <Services/CharacterService.h>
#include <Services/TravelService.h>
#include <Services/DebugService.h>

#include <Events/UpdateEvent.h>

#include <ModCompat/BehaviorVar.h>  
#include <Hooks/MemoryVP.h>

namespace
{
std::unique_ptr<SkyrimBridge> CreateBridgeContext(World& world)
{
    // Create the bridge and register all five interfaces into EnTT context so
    // that services can look them up via ctx().at<IXxxBridge&>().
    //
    // IMPORTANT: With multiple inheritance, entt 3.10.0 ctx().emplace<T&>(obj) stores
    // the raw pointer reinterpreted as T. To get correct virtual dispatch, we must
    // explicitly cast to each interface type before storing.
    auto bridge = std::make_unique<SkyrimBridge>();
    IActorBridge&  actorRef  = *bridge;
    IFormBridge&   formRef   = *bridge;
    IPlayerBridge& playerRef = *bridge;
    IWorldBridge&  worldRef  = *bridge;
    world.ctx().emplace<IActorBridge&>(actorRef);
    world.ctx().emplace<IFormBridge&>(formRef);
    world.ctx().emplace<IPlayerBridge&>(playerRef);
    world.ctx().emplace<IWorldBridge&>(worldRef);
    return bridge;
}
}

World::World()
    : m_pSkyrimBridge(CreateBridgeContext(*this))
    , m_pDialogueBridge(std::make_unique<DialogueBridge>())
    , m_runner(m_dispatcher)
    , m_transport(*this, m_dispatcher)
    , m_lastFrameTime{std::chrono::high_resolution_clock::now()}
{

    ctx().emplace<ImguiService>();
    ctx().emplace<UiService>(*this, ctx().at<IActorBridge&>());
    // Before CharacterService: an arrival drops the copies the old cell took before they are checked.
    ctx().emplace<TravelService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport,
                                   ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(), ctx().at<IPlayerBridge&>());
    ctx().emplace<DebugService>(m_dispatcher, *this, m_transport, ctx().at<ImguiService>());
    ctx().emplace<PapyrusService>(m_dispatcher);
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport,
                                  ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>());
    ctx().emplace<TimeScaleService>(*this, m_dispatcher, m_transport);
    ctx().emplace<SessionService>(*this, m_dispatcher);
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport,
                               ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<EffectVisualsService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<AiStateService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<CutsceneService>(*this, m_dispatcher, m_transport);
    ctx().emplace<DialogueService>(*this, m_dispatcher, m_transport, *m_pDialogueBridge);
    ctx().emplace<CommandService>(m_transport, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport,
                                  ctx().at<IPlayerBridge&>(), ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>());
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher, ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<MapService>(*this, m_dispatcher, m_transport);
    ctx().emplace<HostService>(*this, m_dispatcher);
    ctx().emplace<GuestService>(*this, m_dispatcher);

    BehaviorVar::Get()->Init();
}

World::~World()
{
    // The services live in the registry's context, which is destroyed after our own members: their
    // connections to m_dispatcher would be released against a dispatcher already gone (it crashed
    // hot reloads now and then). They go first, newest first.
    ctx().erase<GuestService>();
    ctx().erase<HostService>();
    ctx().erase<MapService>();
    ctx().erase<CombatService>();
    ctx().erase<PlayerService>();
    ctx().erase<CommandService>();
    ctx().erase<DialogueService>();
    ctx().erase<CutsceneService>();
    ctx().erase<AiStateService>();
    ctx().erase<EffectVisualsService>();
    ctx().erase<MagicService>();
    ctx().erase<InventoryService>();
    ctx().erase<ActorValueService>();
    ctx().erase<SessionService>();
    ctx().erase<TimeScaleService>();
    ctx().erase<ObjectService>();
    ctx().erase<PapyrusService>();
    ctx().erase<DebugService>();
    ctx().erase<CharacterService>();
    ctx().erase<UiService>();
    ctx().erase<ImguiService>();
}

void World::Update() noexcept
{
    const auto cNow = std::chrono::high_resolution_clock::now();
    const auto cDelta = cNow - m_lastFrameTime;
    m_lastFrameTime = cNow;

    const auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(cDelta).count();

    const double cFrameMs = cDeltaSeconds * 1000.0;
    ++m_frameStats.Frames;
    m_frameStats.LongestFrameMs = std::max(m_frameStats.LongestFrameMs, cFrameMs);
    if (cFrameMs > kStallMs && m_frameStats.Frames > 1)
    {
        ++m_frameStats.Stalls;
        spdlog::warn("Frame stalled: {:.0f}ms since the last one (our last update took {:.1f}ms)", cFrameMs, m_lastUpdateMs);
    }

    // Force run this before so we get the tasks scheduled to run
    m_runner.OnUpdate(UpdateEvent(cDeltaSeconds));
    m_dispatcher.trigger(UpdateEvent(cDeltaSeconds));

    m_lastUpdateMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - cNow).count();
    m_frameStats.LongestUpdateMs = std::max(m_frameStats.LongestUpdateMs, m_lastUpdateMs);
    if (m_lastUpdateMs > kStallMs)
        spdlog::warn("Our update took {:.0f}ms", m_lastUpdateMs);
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

void World::Create() noexcept
{
    if (!entt::locator<World>::has_value())
    {
        entt::locator<World>::emplace();
    }
}

World& World::Get() noexcept
{
    return entt::locator<World>::value();
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

DebugService& World::GetDebugService() noexcept
{
    return ctx().at<DebugService>();
}

const DebugService& World::GetDebugService() const noexcept
{
    return ctx().at<const DebugService>();
}

MagicService& World::GetMagicService() noexcept
{
    return ctx().at<MagicService>();
}

const MagicService& World::GetMagicService() const noexcept
{
    return ctx().at<const MagicService>();
}

HostService& World::GetHostService() noexcept
{
    return ctx().at<HostService>();
}

const HostService& World::GetHostService() const noexcept
{
    return ctx().at<const HostService>();
}
