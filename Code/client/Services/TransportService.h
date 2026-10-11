#pragma once

#include "Events/ConnectedEvent.h"
#include "Events/DisconnectedEvent.h"

#include <atomic>
#include <Client.hpp>

struct ImguiService;
struct UpdateEvent;
struct ClientMessage;
struct AuthenticationResponse;
struct NotifySettingsChange;

struct World;
class IPlayerBridge;
class IFormBridge;
class IWorldBridge;

namespace Server
{
struct GameServer;
}

using TiltedPhoques::Client;

/**
 * @brief Handles communication with the server.
 */
struct TransportService : Client
{
    TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~TransportService() noexcept = default;

    TP_NOCOPYMOVE(TransportService);

    bool Send(const ClientMessage& acMessage) const noexcept;

    /**
     * @brief Hosting: talks to the embedded server in this process instead of over a socket.
     * Messages go straight into the server; its replies are queued and handled on our next update.
     */
    void ConnectLocal(Server::GameServer& aServer) noexcept;
    // Both hide Client's: they cover the in-process connection too.
    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] bool IsConnectedLocally() const noexcept { return m_pLocalServer != nullptr; }
    void Close() noexcept;
    /**
     * @brief The session's tick: the server's own when hosting, else our clock synchronized to it.
     */
    [[nodiscard]] uint64_t GetTick() const noexcept;

    void OnConsume(const void* apData, uint32_t aSize) override;
    void OnConnected() override;
    void OnDisconnected(EDisconnectReason aReason) override;
    void OnUpdate() override;

    [[nodiscard]] bool IsOnline() const noexcept { return m_connected; }
    void SetServerPassword(const std::string& acPassword) noexcept { m_serverPassword = acPassword; }
    const uint32_t& GetLocalPlayerId() const noexcept { return m_localPlayerId; }

protected:
    // Event handlers
    void HandleUpdate(const UpdateEvent& acEvent) noexcept;
    void HandleConnected(const ConnectedEvent& acEvent) noexcept;
    void HandleDisconnected(const DisconnectedEvent& acEvent) noexcept;

    // Packet handlers
    void HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept;
    void HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept;

private:
    World& m_world;
    entt::dispatcher& m_dispatcher;
    IPlayerBridge& m_playerBridge;
    IFormBridge& m_formBridge;
    IWorldBridge& m_worldBridge;
    bool m_connected;
    Server::GameServer* m_pLocalServer{nullptr};
    Vector<Vector<uint8_t>> m_localInbox;
    String m_serverPassword{};
    uint32_t m_localPlayerId;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_sendServerMessageConnection;
    entt::scoped_connection m_settingsChangeConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    std::function<void(UniquePtr<ServerMessage>&)> m_messageHandlers[kServerOpcodeMax];
};
