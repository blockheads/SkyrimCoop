#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

struct World;
struct TransportService;
struct ConnectedEvent;
struct DisconnectedEvent;
struct RoutedCallEvent;
struct CallEndedEvent;
struct NotifyPlayerLeft;

/**
 * @brief The script calls this game sends away (docs/design/party-calls.md): every RoutedCallEvent goes to the host as
 * a RoutedCallRequest. The party's from the other games (cutscenes.md: the controls and the screen; shows,
 * progression) run here, on this game's player.
 */
struct CutsceneService
{
    CutsceneService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~CutsceneService() noexcept = default;

    NO_COPY_MOVE(CutsceneService);

protected:
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnRoutedCall(const RoutedCallEvent& acEvent) noexcept;
    void OnNotifyRoutedCall(const NotifyRoutedCall& acMessage) noexcept;
    void OnCallEnded(const CallEndedEvent& acEvent) noexcept;
    void OnNotifyCallEnded(const NotifyCallEnded& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;

private:
    World& m_world;
    TransportService& m_transport;

    std::vector<entt::scoped_connection> m_connections;
};
