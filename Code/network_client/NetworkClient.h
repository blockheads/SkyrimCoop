#pragma once

#include <Client.hpp>
#include <cstdint>
#include <entt/entt.hpp>

struct ServerMessage;
struct ClientMessage;

/**
 * @brief Pure networking layer for P2P client.
 *
 * NetworkClient is a thin wrapper around TiltedPhoques::Client that:
 * - Connects to ONE host
 * - Receives ServerMessages from host and dispatches events
 * - Sends ClientMessages to host
 *
 * NO game logic - just networking! Services handle all game state updates.
 *
 * Flow:
 * 1. Host sends ServerMessage → NetworkClient receives
 * 2. NetworkClient triggers event via dispatcher
 * 3. Service listens to event → updates World
 * 4. Service calls NetworkClient->SendToHost() to send requests
 */
class NetworkClient : public TiltedPhoques::Client
{
public:
    /**
     * @brief Construct NetworkClient with event dispatcher
     * @param aDispatcher Event dispatcher for triggering message events
     */
    explicit NetworkClient(entt::dispatcher& aDispatcher) noexcept;
    ~NetworkClient() override;

    TP_NOCOPYMOVE(NetworkClient);

    /**
     * @brief Connect to a host
     * @param aHostAddress Host IP address (e.g., "192.168.1.5")
     * @param aPort Host port (default: 10578)
     * @return true if connection initiated successfully
     */
    bool ConnectToHost(const TiltedPhoques::String& aHostAddress, uint16_t aPort = 10578) noexcept;

    /**
     * @brief Disconnect from host
     */
    void Disconnect() noexcept;

    /**
     * @brief Update network (process messages from host)
     * Called every frame by World
     */
    void Update() noexcept;

    /**
     * @brief Send a message to the host
     * @param aMessage The ClientMessage to send
     */
    void SendToHost(const ClientMessage& aMessage) noexcept;

    /**
     * @brief Check if connected to host
     */
    [[nodiscard]] bool IsConnected() const noexcept { return m_isConnected; }

protected:
    // TiltedPhoques::Client interface
    void OnUpdate() override;
    void OnConsume(const void* apData, uint32_t aSize) override;
    void OnConnected() override;
    void OnDisconnected(uint64_t aReasonCode) override;
    void OnAuthenticated() override;

private:
    /**
     * @brief Handle incoming server message from host
     * @param apMessage Parsed server message
     */
    void HandleServerMessage(UniquePtr<ServerMessage>& apMessage) noexcept;

    /**
     * @brief Register message handlers (called during construction)
     */
    void BindMessageHandlers() noexcept;

private:
    entt::dispatcher& m_dispatcher; // Event dispatcher (NOT owned!)

    bool m_isConnected{false};
};
