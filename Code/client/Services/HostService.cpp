#include <Services/HostService.h>

#include <GameServer.h>
#include <World.h>
#include <Events/UpdateEvent.h>
#include <Console/ConsoleRegistry.h>

HostService::HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>()
        .connect<&HostService::OnUpdate>(this);
}

HostService::~HostService() noexcept
{
    StopHosting();
}

bool HostService::StartHosting(uint16_t aPort, uint8_t aMaxPlayers) noexcept
{
    if (m_isHosting)
    {
        spdlog::warn("[HostService] Already hosting a session");
        return true;
    }

    // NOTE: World class name collision has been RESOLVED!
    // All server code is now wrapped in Server namespace (Server::World, Server::GameServer, etc.)
    // This allows client and server to coexist in the same binary without conflicts.

    try
    {
        spdlog::info("[HostService] Starting P2P host session on port {} for up to {} players", aPort, aMaxPlayers);

        // Create console registry for embedded server
        m_pConsole = std::make_unique<ServerConsole::ConsoleRegistry>("EmbeddedServer");

        // Create embedded GameServer instance (now properly namespaced!)
        // NOTE: GameServer constructor automatically calls Host() internally!
        m_pGameServer = std::make_unique<Server::GameServer>(*m_pConsole);

        m_isHosting = true;

        spdlog::info("[HostService] Successfully started hosting session on port {}", m_pGameServer->GetPort());
        spdlog::info("[HostService] Host player can connect to localhost (127.0.0.1:{})", m_pGameServer->GetPort());
        spdlog::info("[HostService] Remote players can connect to this machine's IP:{})", m_pGameServer->GetPort());

        return true;
    }
    catch (const std::exception& e)
    {
        spdlog::error("[HostService] Exception while starting hosting: {}", e.what());
        m_pGameServer.reset();
        m_pConsole.reset();
        return false;
    }
}

void HostService::StopHosting() noexcept
{
    if (!m_isHosting)
        return;

    spdlog::info("[HostService] Stopping host session");

    if (m_pGameServer)
    {
        m_pGameServer->Close();
        m_pGameServer.reset();
    }

    m_pConsole.reset();
    m_isHosting = false;

    spdlog::info("[HostService] Host session stopped");
}

void HostService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_isHosting && m_pGameServer)
    {
        // Update embedded server every frame
        // The server's Update() method handles:
        // - Processing incoming packets from clients
        // - Running game logic
        // - Broadcasting updates to all connected clients
        m_pGameServer->Update();
    }
}
