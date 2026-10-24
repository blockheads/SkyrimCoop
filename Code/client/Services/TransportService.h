#pragma once

#include <Events/Listen.h>
#include "Events/ConnectedEvent.h"
#include "Events/DisconnectedEvent.h"
#include "Services/JoinStatus.h"

#include <atomic>
#include <network/Client.h>
#include <Messages/Message.h>
#include <NoCopyMove.h>

struct ImguiService;
struct UpdateEvent;
struct RequestMessage;
struct AuthenticationResponse;

struct World;
class IPlayerBridge;
class IFormBridge;
class IWorldBridge;

struct HostNetwork;

using Net::Client;

/**
 * @brief Handles communication with the host.
 */
struct TransportService : Client
{
    TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~TransportService() noexcept = default;

    NO_COPY_MOVE(TransportService);

    bool Send(const RequestMessage& acMessage) const noexcept;

    /**
     * @brief Hosting: talks to the host's network in this process instead of over a socket.
     * Messages go straight to the host's rules; their replies are queued and handled on our next update.
     */
    void ConnectLocal(HostNetwork& aNetwork) noexcept;
    // Joins the session at acEndpoint (hides Client's: the join status follows it).
    bool Connect(const std::string& acEndpoint) noexcept;
    // Both hide Client's: they cover the in-process connection too.
    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] bool IsConnectedLocally() const noexcept { return m_pHostNetwork != nullptr; }
    // aWhy: why the session ends for this game (a Steam host lost is kLost: the join service reconnects).
    void Close(DisconnectedEvent::Cause aWhy = DisconnectedEvent::Cause::kLeft) noexcept;
    // Where the last Connect dialled.
    [[nodiscard]] const std::string& GetEndpoint() const noexcept { return m_endpoint; }
    /**
     * @brief The session's tick: the host's network's own when hosting, else our clock synchronized to it.
     */
    [[nodiscard]] uint64_t GetTick() const noexcept;

    void OnConsume(const void* apData, uint32_t aSize) override;
    void OnConnected() override;
    void OnDisconnected(EDisconnectReason aReason) override;
    void OnUpdate() override;

    [[nodiscard]] bool IsOnline() const noexcept { return m_connected; }
    const uint32_t& GetLocalPlayerId() const noexcept { return m_localPlayerId; }
    // The last attempt to join: under way, joined, or refused and why.
    [[nodiscard]] const JoinStatus& GetJoinStatus() const noexcept { return m_joinStatus; }

protected:
    // Event handlers
    void HandleUpdate(const UpdateEvent& acEvent) noexcept;
    void HandleConnected(const ConnectedEvent& acEvent) noexcept;
    void HandleDisconnected(const DisconnectedEvent& acEvent) noexcept;

    // Packet handlers
    void HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept;

private:
    World& m_world;
    entt::dispatcher& m_dispatcher;
    IPlayerBridge& m_playerBridge;
    IFormBridge& m_formBridge;
    IWorldBridge& m_worldBridge;
    bool m_connected;
    HostNetwork* m_pHostNetwork{nullptr};
    std::vector<std::vector<uint8_t>> m_localInbox;
    uint32_t m_localPlayerId;
    JoinStatus m_joinStatus;
    std::string m_endpoint;
    std::optional<DisconnectedEvent::Cause> m_closing; // set while Close runs: the cause it gives
    bool m_closingRoute{};                             // Close is ending the host's own in-process route

    std::vector<entt::scoped_connection> m_connections;
    std::function<void(std::unique_ptr<NotifyMessage>&)> m_messageHandlers[kNotifyOpcodeMax];
};
