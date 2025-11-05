#pragma once

#include <Server.hpp>
#include <cstdint>
#include <entt/entt.hpp>

using TiltedPhoques::ConnectionId_t;

struct ServerMessage;
struct ClientMessage;

/**
 * @brief Pure networking layer for P2P host.
 *
 * NetworkBridge is a thin wrapper around TiltedPhoques::Server that:
 * - Listens for peer connections
 * - Receives ClientMessages from peers and dispatches events
 * - Sends ServerMessages to peers
 *
 * NO game logic - just networking! Services handle all game state updates.
 *
 * Flow:
 * 1. Peer sends ClientMessage → NetworkBridge receives
 * 2. NetworkBridge triggers event via dispatcher
 * 3. Service listens to event → updates Server::World
 * 4. Service calls NetworkBridge->BroadcastToAllPeers() to send response
 */
class NetworkBridge : public TiltedPhoques::Server
{
public:
    /**
     * @brief Construct NetworkBridge with event dispatcher
     * @param aDispatcher Event dispatcher for triggering message events
     */
    explicit NetworkBridge(entt::dispatcher& aDispatcher) noexcept;
    ~NetworkBridge() override;

    TP_NOCOPYMOVE(NetworkBridge);

    /**
     * @brief Start listening for peer connections
     * @param aPort Port to listen on (default: 10578)
     * @param aMaxPeers Maximum number of peers to accept
     * @return true if started successfully
     */
    bool StartListening(uint16_t aPort = 10578, uint8_t aMaxPeers = 8) noexcept;

    /**
     * @brief Stop listening and disconnect all peers
     */
    void StopListening() noexcept;

    /**
     * @brief Update network (process messages, send updates)
     * Called every frame by HostService
     */
    void Update() noexcept;

    /**
     * @brief Broadcast a message to all connected peers
     * @param aMessage The ServerMessage to send to all peers
     */
    void BroadcastToAllPeers(const ServerMessage& aMessage) const noexcept;

    /**
     * @brief Send a message to a specific peer
     * @param aConnectionId Target peer connection
     * @param aMessage The ServerMessage to send
     */
    void SendToPeer(ConnectionId_t aConnectionId, const ServerMessage& aMessage) const noexcept;

    /**
     * @brief Get number of connected peers
     */
    [[nodiscard]] size_t GetPeerCount() const noexcept;

    /**
     * @brief Check if currently listening for connections
     */
    [[nodiscard]] bool IsListening() const noexcept { return m_isListening; }

protected:
    // TiltedPhoques::Server interface
    void OnUpdate() override;
    void OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId) override;
    void OnConnection(ConnectionId_t aConnectionId) override;
    void OnDisconnection(ConnectionId_t aConnectionId, EDisconnectReason aReason) override;

private:
    /**
     * @brief Handle incoming client message
     * @param apMessage Parsed client message
     * @param aConnectionId Peer that sent the message
     */
    void HandleClientMessage(UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId) noexcept;

    /**
     * @brief Register message handlers (called during construction)
     */
    void BindMessageHandlers() noexcept;

private:
    entt::dispatcher& m_dispatcher; // Event dispatcher (NOT owned!)

    bool m_isListening{false};

    // Message handlers: ClientOpcode -> handler function (same pattern as GameServer)
    std::function<void(UniquePtr<ClientMessage>&, ConnectionId_t)> m_messageHandlers[256]; // TODO: Use proper kClientOpcodeMax

    // Peer tracking (simple for now, can expand later)
    TiltedPhoques::Map<ConnectionId_t, TiltedPhoques::String> m_peerUsernames;
};
