#pragma once

#include <Server.hpp>

#include <functional>

struct RequestMessage;
struct NotifyMessage;

/**
 * @brief The host's end of the session's connections: the enet listener the other players' games
 * join, and in-process routes (the host's own game; the bridge tests' games). Each message a game
 * sends goes to the front door (HostAuthority).
 */
struct HostNetwork final : TiltedPhoques::Server
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;
    // True: the message was handled.
    using FrontDoor = std::function<bool(const RequestMessage&, ConnectionId_t)>;
    using Disconnected = std::function<void(ConnectionId_t)>;
    // A message for an in-process game, serialized as on the wire.
    using Inbox = std::function<void(const void*, uint32_t)>;

    HostNetwork() noexcept = default;

    TP_NOCOPYMOVE(HostNetwork);

    // Listens on aPort, or the next free one above it.
    bool Listen(uint16_t aPort) noexcept;

    void SetFrontDoor(FrontDoor aFrontDoor) noexcept { m_frontDoor = std::move(aFrontDoor); }
    void SetDisconnectionHandler(Disconnected aHandler) noexcept { m_disconnected = std::move(aHandler); }

    // A game in this process; the first one opened is the host's own.
    void OpenRoute(ConnectionId_t aConnection, Inbox aInbox) noexcept;
    void CloseRoute(ConnectionId_t aConnection) noexcept;
    [[nodiscard]] ConnectionId_t HostConnection() const noexcept { return m_hostConnection; }

    // A message from the game on aConnection.
    void Receive(const void* apData, uint32_t aSize, ConnectionId_t aConnection) noexcept;
    void Send(ConnectionId_t aConnection, const NotifyMessage& acMessage, TiltedPhoques::EPacketFlags aFlags = TiltedPhoques::kReliable) const noexcept;

protected:
    void OnUpdate() override {}
    void OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnection) override { Receive(apData, aSize, aConnection); }
    void OnConnection(ConnectionId_t aConnection) override;
    void OnDisconnection(ConnectionId_t aConnection, EDisconnectReason aReason) override;

private:
    FrontDoor m_frontDoor;
    Disconnected m_disconnected;
    TiltedPhoques::Map<ConnectionId_t, Inbox> m_routes;
    ConnectionId_t m_hostConnection{0};
};
