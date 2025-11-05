#include <World.h>
#include <Components.h>

#include <Services/CharacterService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/ActorValueService.h>
#include <Services/AdminService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/OverlayService.h>
#include <Services/CommandService.h>
#include <Services/StringCacheService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/ScriptService.h>

// P2P: Client services needed for host
#include <Services/ImguiService.h>
#include <Services/InputService.h>
#include <Services/DebugService.h>
#include <Services/PapyrusService.h>
#include <Services/DiscordService.h>

#include <Events/PreUpdateEvent.h>
#include <Events/UpdateEvent.h>

#include <NetworkBridge.h>

#include <es_loader/ESLoader.h>

namespace Server
{

World::World()
    : m_runner(m_dispatcher)
    , m_modSystem(m_dispatcher)
{
    m_spAdminService = std::make_shared<AdminService>(*this, m_dispatcher);
    spdlog::default_logger()->sinks().push_back(std::static_pointer_cast<spdlog::sinks::sink>(m_spAdminService));

    // P2P: Client services needed for host to interact with Skyrim
    ctx().emplace<ImguiService>();
    ctx().emplace<OverlayService>(*this, m_dispatcher);
    ctx().emplace<InputService>(ctx().at<OverlayService>());
    ctx().emplace<DebugService>(m_dispatcher, *this, ctx().at<ImguiService>());
    ctx().emplace<PapyrusService>(m_dispatcher);
    ctx().emplace<DiscordService>(m_dispatcher);

    // Existing server services
    ctx().emplace<CharacterService>(*this, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher);
    ctx().emplace<CalendarService>(*this, m_dispatcher);
    ctx().emplace<ObjectService>(*this, m_dispatcher);
    ctx().emplace<ModsComponent>();
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<PartyService>(*this, m_dispatcher);
    ctx().emplace<ActorValueService>(*this, m_dispatcher);
    ctx().emplace<InventoryService>(*this, m_dispatcher);
    ctx().emplace<MagicService>(*this, m_dispatcher);
    ctx().emplace<OverlayService>(*this, m_dispatcher);
    ctx().emplace<CommandService>(*this, m_dispatcher);
    ctx().emplace<StringCacheService>(*this, m_dispatcher);
    ctx().emplace<CombatService>(*this, m_dispatcher);
    ctx().emplace<WeatherService>(*this, m_dispatcher);

    ESLoader::ESLoader loader;
    // emplace loaded mods into modscomponent.
    m_recordCollection = loader.BuildRecordCollection();
    for (const auto& it : loader.GetLoadOrder())
    {
        ctx().emplace<ModsComponent>().AddServerMod(it);
    }

    // late initialize the ScriptService to ensure all components are valid
    m_pScriptService = TiltedPhoques::MakeUnique<ScriptService>(*this, m_dispatcher);
}

World::~World()
{
    m_pScriptService.reset();
}

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

    // P2P: Update network bridge if hosting
    if (m_pNetworkBridge)
    {
        m_pNetworkBridge->Update();
    }
}

bool World::StartHosting(uint16_t aPort, uint8_t aMaxPeers) noexcept
{
    if (m_pNetworkBridge && m_pNetworkBridge->IsListening())
    {
        spdlog::warn("[Server::World] Already hosting a session");
        return true;
    }

    try
    {
        spdlog::info("[Server::World] Starting P2P host session on port {} for up to {} peers", aPort, aMaxPeers);

        // Create NetworkBridge with dispatcher (no World reference needed - pure networking!)
        m_pNetworkBridge = std::make_unique<NetworkBridge>(m_dispatcher);
        if (!m_pNetworkBridge->StartListening(aPort, aMaxPeers))
        {
            spdlog::error("[Server::World] Failed to start listening on port {}", aPort);
            m_pNetworkBridge.reset();
            return false;
        }

        spdlog::info("[Server::World] Successfully started hosting on port {}", aPort);
        return true;
    }
    catch (const std::exception& e)
    {
        spdlog::error("[Server::World] Exception while starting hosting: {}", e.what());
        m_pNetworkBridge.reset();
        return false;
    }
}

void World::StopHosting() noexcept
{
    if (!m_pNetworkBridge)
        return;

    spdlog::info("[Server::World] Stopping hosting session");
    m_pNetworkBridge->StopListening();
    m_pNetworkBridge.reset();
    spdlog::info("[Server::World] Host session stopped");
}

bool World::IsHosting() const noexcept
{
    return m_pNetworkBridge && m_pNetworkBridge->IsListening();
}

World* World::Create() noexcept
{
    auto* pWorld = new World();
    WorldBase::Set(pWorld);
    return pWorld;
}

} // namespace Server
