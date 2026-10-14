#include <TiltedOnlinePCH.h>

#include "World.h"

// SkyrimBridge is included here (not in World.h) to keep SKSE headers out of
// every service that includes World.h.
#include <Bridges/SkyrimBridge.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Bridges/IEventBridge.h>
#include <Bridges/IWorldBridge.h>

#ifdef HAS_CEF
#include <Services/InputService.h>
#endif
#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Services/ImguiService.h>
#ifdef HAS_RMLUI
#include <Ui/UiService.h>
#endif
#include <Services/PapyrusService.h>
#include <Services/DiscordService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/ActorValueService.h>
#include <Services/InventoryService.h>
#include <Services/EffectVisualsService.h>
#include <Services/MagicService.h>
#include <Services/CommandService.h>
#include <Services/CalendarService.h>
#include <Services/PlayerService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/MapService.h>
#include <Services/HostService.h>

#include <Events/PreUpdateEvent.h>
#include <Events/UpdateEvent.h>

#include <ModCompat/BehaviorVar.h>  

namespace
{
std::unique_ptr<SkyrimBridge> CreateBridgeContext(World& world)
{
    // Create the bridge and register all five interfaces into EnTT context so
    // that services can look them up via ctx().at<IXxxBridge&>().
    //
    // IMPORTANT: With multiple inheritance, entt 3.10.0 ctx().emplace<T&>(obj) stores
    // the raw pointer reinterpreted as T. To get correct virtual dispatch, we must
    // explicitly cast to each interface type before storing (discovered via ReplayBridge tests).
    auto bridge = std::make_unique<SkyrimBridge>();
    IActorBridge&  actorRef  = *bridge;
    IFormBridge&   formRef   = *bridge;
    IPlayerBridge& playerRef = *bridge;
    IEventBridge&  eventRef  = *bridge;
    IWorldBridge&  worldRef  = *bridge;
    world.ctx().emplace<IActorBridge&>(actorRef);
    world.ctx().emplace<IFormBridge&>(formRef);
    world.ctx().emplace<IPlayerBridge&>(playerRef);
    world.ctx().emplace<IEventBridge&>(eventRef);
    world.ctx().emplace<IWorldBridge&>(worldRef);
    return bridge;
}
}

World::World()
    : m_pSkyrimBridge(CreateBridgeContext(*this))
    , m_runner(m_dispatcher)
    , m_transport(*this, m_dispatcher)
    , m_modSystem(m_dispatcher)
    , m_lastFrameTime{std::chrono::high_resolution_clock::now()}
{

    ctx().emplace<ImguiService>();
#ifdef HAS_RMLUI
    ctx().emplace<UiService>(*this, ctx().at<IActorBridge&>());
#endif
    ctx().emplace<OverlayService>(*this, m_transport, m_dispatcher,
                                  ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(), ctx().at<IPlayerBridge&>());
#ifdef HAS_CEF
    ctx().emplace<InputService>(ctx().at<OverlayService>());
#endif
    // Before CharacterService: an arrival drops the copies the old cell took before they are checked.
    ctx().emplace<TravelService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport,
                                   ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(), ctx().at<IPlayerBridge&>());
#if defined(HAS_DIRECTXTK) && !defined(__MINGW32__)
    ctx().emplace<DebugService>(m_dispatcher, *this, m_transport, ctx().at<ImguiService>());
#endif
    ctx().emplace<PapyrusService>(m_dispatcher);
    ctx().emplace<DiscordService>(m_dispatcher, ctx().at<IPlayerBridge&>());
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport,
                                  ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>(), ctx().at<IEventBridge&>());
    ctx().emplace<CalendarService>(*this, m_dispatcher, m_transport);
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<SessionService>(*this, m_dispatcher);
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport,
                               ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<EffectVisualsService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<CommandService>(*this, m_transport, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport,
                                  ctx().at<IPlayerBridge&>(), ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>());
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher, ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<WeatherService>(*this, m_dispatcher);
    ctx().emplace<MapService>(*this, m_dispatcher, m_transport);
    ctx().emplace<HostService>(*this, m_dispatcher);

    BehaviorVar::Get()->Init();
}

World::~World()
{
    // The services live in the registry's context, which is destroyed after our own members: their
    // connections to m_dispatcher would be released against a dispatcher already gone (it crashed
    // hot reloads now and then). They go first, newest first.
    ctx().erase<HostService>();
    ctx().erase<MapService>();
    ctx().erase<WeatherService>();
    ctx().erase<CombatService>();
    ctx().erase<PlayerService>();
    ctx().erase<CommandService>();
    ctx().erase<EffectVisualsService>();
    ctx().erase<MagicService>();
    ctx().erase<InventoryService>();
    ctx().erase<ActorValueService>();
    ctx().erase<SessionService>();
    ctx().erase<QuestService>();
    ctx().erase<CalendarService>();
    ctx().erase<ObjectService>();
    ctx().erase<DiscordService>();
    ctx().erase<PapyrusService>();
#if defined(HAS_DIRECTXTK) && !defined(__MINGW32__)
    ctx().erase<DebugService>();
#endif
    ctx().erase<CharacterService>();
#ifdef HAS_CEF
    ctx().erase<InputService>();
#endif
    ctx().erase<OverlayService>();
#ifdef HAS_RMLUI
    ctx().erase<UiService>();
#endif
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

    m_dispatcher.trigger(PreUpdateEvent(cDeltaSeconds));

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

ModSystem& World::GetModSystem() noexcept
{
    return m_modSystem;
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

IEventBridge& World::GetEventBridge() noexcept
{
    return ctx().at<IEventBridge&>();
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
