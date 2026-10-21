#pragma once

#include <memory>
#include <spdlog/spdlog.h>

struct World;
struct UpdateEvent;
struct ActivateEvent;
struct ScriptAnimationEvent;
struct ContainerChangeEvent;

namespace Server
{
    struct GameServer;
}

namespace ServerConsole { class ConsoleRegistry; }
struct HostAuthority;
class HostEngine;



/**
 * @brief Manages embedded GameServer when player is hosting a P2P session.
 *
 * This service enables the host-client (listen server) P2P model where one player
 * runs both the client and server simultaneously. The host player is the authoritative
 * source of truth for world state, NPCs, and quest progression.
 *
 * Architecture:
 * - Host player: Runs client + embedded GameServer (this service)
 * - Remote players: Run client only, connect to host's IP
 */
struct HostService
{
    HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~HostService() noexcept;

    TP_NOCOPYMOVE(HostService);

    /**
     * @brief Start hosting a P2P session with embedded GameServer
     * @param aPort Server port (default: 10578)
     * @param aMaxPlayers Maximum number of players including host (default: 8)
     * @return true if hosting started successfully, false otherwise
     */
    bool StartHosting(uint16_t aPort = 10578, uint8_t aMaxPlayers = 8) noexcept;

    /**
     * @brief Stop hosting and shutdown the embedded GameServer
     */
    void StopHosting() noexcept;

    /**
     * @brief Check if currently hosting a session
     * @return true if hosting, false otherwise
     */
    [[nodiscard]] bool IsHosting() const noexcept { return m_isHosting; }

    /**
     * @brief Get the embedded GameServer instance
     * @return Pointer to GameServer, or nullptr if not hosting
     */
    [[nodiscard]] Server::GameServer* GetServer() const noexcept { return m_pGameServer.get(); }

    // Removes a player from the session (hosting only).
    void KickPlayer(uint32_t aPlayerId) noexcept;

protected:
    /**
     * @brief Update the embedded server every frame
     * @param acEvent Update event with delta time
     */
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    // The host's world scripts' events and containers' items for HostWorldState: doors, animations, item counts
    // (docs/design/world-state-sync.md steps 5-6). State (enabled, locks) HostEngine reads from the change funnel itself.
    void OnActivate(const ActivateEvent& acEvent) noexcept;
    void OnScriptAnimation(const ScriptAnimationEvent& acEvent) noexcept;
    void OnContainerChange(const ContainerChangeEvent& acEvent) noexcept;

private:
    World& m_world;
    entt::dispatcher& m_dispatcher;

    bool m_isHosting{false};
    bool m_isShuttingDown{false};
    // Declared first so they outlive the server, which calls into the authority, which reads the engine.
    std::unique_ptr<HostEngine> m_pEngine;
    std::unique_ptr<HostAuthority> m_pAuthority;
    std::unique_ptr<Server::GameServer> m_pGameServer;
    std::unique_ptr<ServerConsole::ConsoleRegistry> m_pConsole;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_scriptAnimationConnection;
    entt::scoped_connection m_containerChangeConnection;
};
