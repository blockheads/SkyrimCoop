#include <Services/HostService.h>

#include <Console/ConsoleRegistry.h>
#include <Events/UpdateEvent.h>
#include <GameServer.h>
#include <World.h>

#include <spdlog/sinks/stdout_color_sinks.h>

HostService::HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&HostService::OnUpdate>(this);
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

        // Create logger for embedded server if it doesn't exist
        if (!spdlog::get("EmbeddedServer"))
        {
            auto embeddedLogger = spdlog::stdout_color_mt("EmbeddedServer");
            embeddedLogger->set_pattern("[EmbeddedServer] [%l] %v");
        }

        // Create console registry for embedded server
        m_pConsole = std::make_unique<ServerConsole::ConsoleRegistry>("EmbeddedServer");

        // Create embedded GameServer instance (now properly namespaced!)
        // NOTE: GameServer constructor automatically calls Host() internally!
        m_pGameServer = std::make_unique<Server::GameServer>(*m_pConsole);

        // Initialize server systems (ModPolicy, console commands, Lua scripting)
        m_pGameServer->Initialize();

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
    if (!m_isHosting || m_isShuttingDown)
        return;

    spdlog::info("[HostService] Requesting server shutdown");

    if (m_pGameServer)
    {
        // Tell the server to shut down on its next update cycle
        // This sets m_requestStop, which causes OnUpdate() to call Close()
        m_pGameServer->Kill();
        m_isShuttingDown = true;

        // The server will finish shutting down asynchronously in OnUpdate()
        // Once IsRunning() returns false, we'll destroy it and create a fresh one next time
    }
}

void HostService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_pGameServer)
    {
        // Check if server has finished shutting down BEFORE calling Update()
        // IsListening() returns false after Close() has been called
        if (m_isShuttingDown && !m_pGameServer->IsListening())
        {
            spdlog::info("[HostService] Server has shut down, destroying instance");

            // Server has closed all connections and sockets
            // Safe to destroy and clear singleton
            m_pGameServer.reset();
            m_pConsole.reset();

            m_isHosting = false;
            m_isShuttingDown = false;

            spdlog::info("[HostService] Host session stopped");
        }
        else
        {
            // Only update the server if it's still running
            // This allows Kill() -> OnUpdate() -> Close() to happen
            static uint32_t sUpdateCounter = 0;
            if (++sUpdateCounter % 600 == 0)  // Log every ~10 seconds at 60fps
            {
                spdlog::debug("[HostService] Server update tick (count: {}), Client count: {}",
                             sUpdateCounter, m_pGameServer->GetClientCount());
            }
            m_pGameServer->Update();
        }
    }
}
