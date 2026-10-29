#pragma once

#include <network/Server.h>

#include <functional>
#include <NoCopyMove.h>

struct RequestMessage;
struct NotifyMessage;
struct RecipeBook;

/**
 * @brief The host's end of the session's connections: the enet listener the other players' games
 * join, and in-process routes (the host's own game; the bridge tests' games). Each message a game
 * sends goes to the front door (HostAuthority).
 */
struct HostNetwork final : Net::Server
{
    using ConnectionId_t = Net::ConnectionId_t;
    // True: the message was handled.
    using FrontDoor = std::function<bool(const RequestMessage&, ConnectionId_t)>;
    using Disconnected = std::function<void(ConnectionId_t)>;
    // A message for an in-process game, serialized as on the wire.
    using Inbox = std::function<void(const void*, uint32_t)>;

    HostNetwork() noexcept = default;

    NO_COPY_MOVE(HostNetwork);

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
    // First the recipes that game has not had (NotifyRecipes), then the message.
    void Send(ConnectionId_t aConnection, const NotifyMessage& acMessage, Net::EPacketFlags aFlags = Net::kReliable) const noexcept;

    // The host's game's recipes (RecipeBook), which the other games get before a message that may name one.
    void SetRecipes(RecipeBook* apRecipes) noexcept { m_pRecipes = apRecipes; }
    [[nodiscard]] RecipeBook* GetRecipes() const noexcept { return m_pRecipes; }

protected:
    void OnUpdate() override {}
    void OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnection) override { Receive(apData, aSize, aConnection); }
    void OnConnection(ConnectionId_t aConnection) override;
    void OnDisconnection(ConnectionId_t aConnection, EDisconnectReason aReason) override;

private:
    void SendNow(ConnectionId_t aConnection, const NotifyMessage& acMessage, Net::EPacketFlags aFlags) const noexcept;

    FrontDoor m_frontDoor;
    Disconnected m_disconnected;
    std::unordered_map<ConnectionId_t, Inbox> m_routes;
    ConnectionId_t m_hostConnection{0};
    RecipeBook* m_pRecipes{nullptr};
    mutable std::unordered_map<ConnectionId_t, size_t> m_recipesSent; // how many of the book each game has had
};
