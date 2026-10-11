#include <Services/SessionService.h>

#include <Events/DisconnectedEvent.h>

#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>

SessionService::SessionService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
{
    m_playerJoinedConnection = aDispatcher.sink<NotifyPlayerJoined>().connect<&SessionService::OnPlayerJoined>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&SessionService::OnPlayerLeft>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&SessionService::OnDisconnected>(this);
}

void SessionService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    spdlog::info("[SessionService] {} ({}) joined{}", acMessage.Username.c_str(), acMessage.PlayerId, acMessage.IsHost ? ", the host" : "");
    m_players[acMessage.PlayerId] = {acMessage.Username, acMessage.IsHost};
}

void SessionService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    spdlog::info("[SessionService] {} ({}) left", acMessage.Username.c_str(), acMessage.PlayerId);
    m_players.erase(acMessage.PlayerId);
}

void SessionService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_players.clear();
}
