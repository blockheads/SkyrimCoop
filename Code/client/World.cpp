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
#include <Services/PapyrusService.h>
#include <Services/DiscordService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/ActorValueService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/CommandService.h>
#include <Services/CalendarService.h>
#include <Services/StringCacheService.h>
#include <Services/PlayerService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/HostService.h>

#include <Events/PreUpdateEvent.h>
#include <Events/UpdateEvent.h>

#include <ModCompat/BehaviorVar.h>  

World::World()
    : m_runner(m_dispatcher)
    , m_transport(*this, m_dispatcher)
    , m_modSystem(m_dispatcher)
    , m_lastFrameTime{std::chrono::high_resolution_clock::now()}
{
    // Create the bridge and register all five interfaces into EnTT context so
    // that services can look them up via ctx().at<IXxxBridge&>().
    m_pSkyrimBridge = std::make_unique<SkyrimBridge>();
    ctx().emplace<IActorBridge&>(*m_pSkyrimBridge);
    ctx().emplace<IFormBridge&>(*m_pSkyrimBridge);
    ctx().emplace<IPlayerBridge&>(*m_pSkyrimBridge);
    ctx().emplace<IEventBridge&>(*m_pSkyrimBridge);
    ctx().emplace<IWorldBridge&>(*m_pSkyrimBridge);

    ctx().emplace<ImguiService>();
    ctx().emplace<OverlayService>(*this, m_transport, m_dispatcher,
                                  ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(), ctx().at<IPlayerBridge&>());
#ifdef HAS_CEF
    ctx().emplace<InputService>(ctx().at<OverlayService>());
#endif
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport);
    ctx().emplace<DebugService>(m_dispatcher, *this, m_transport, ctx().at<ImguiService>());
    ctx().emplace<PapyrusService>(m_dispatcher);
    ctx().emplace<DiscordService>(m_dispatcher, ctx().at<IPlayerBridge&>());
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport,
                                  ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>(), ctx().at<IEventBridge&>());
    ctx().emplace<CalendarService>(*this, m_dispatcher, m_transport);
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<PartyService>(*this, m_dispatcher, m_transport);
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport);
    ctx().emplace<CommandService>(*this, m_transport, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport);
    ctx().emplace<StringCacheService>(m_dispatcher);
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher, ctx().at<IFormBridge&>());
    ctx().emplace<WeatherService>(*this, m_transport, m_dispatcher);
    ctx().emplace<HostService>(*this, m_dispatcher);

    BehaviorVar::Get()->Init();
}

World::~World() = default;

void World::Update() noexcept
{
    const auto cNow = std::chrono::high_resolution_clock::now();
    const auto cDelta = cNow - m_lastFrameTime;
    m_lastFrameTime = cNow;

    const auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(cDelta).count();

    m_dispatcher.trigger(PreUpdateEvent(cDeltaSeconds));

    // Force run this before so we get the tasks scheduled to run
    m_runner.OnUpdate(UpdateEvent(cDeltaSeconds));
    m_dispatcher.trigger(UpdateEvent(cDeltaSeconds));
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
    return m_transport.GetClock().GetCurrentTick();
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
