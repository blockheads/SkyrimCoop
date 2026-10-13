#include <Services/SessionService.h>

#include <Events/DisconnectedEvent.h>

#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerLevel.h>

SessionService::SessionService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
{
    m_playerJoinedConnection = aDispatcher.sink<NotifyPlayerJoined>().connect<&SessionService::OnPlayerJoined>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&SessionService::OnPlayerLeft>(this);
    m_playerLevelConnection = aDispatcher.sink<NotifyPlayerLevel>().connect<&SessionService::OnPlayerLevel>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&SessionService::OnDisconnected>(this);
}

void SessionService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    spdlog::info("[SessionService] {} ({}) joined{}", acMessage.Username.c_str(), acMessage.PlayerId, acMessage.IsHost ? ", the host" : "");
    m_players[acMessage.PlayerId] = {acMessage.Username, acMessage.IsHost, acMessage.Level};
}

void SessionService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    spdlog::info("[SessionService] {} ({}) left", acMessage.Username.c_str(), acMessage.PlayerId);
    m_players.erase(acMessage.PlayerId);
}

void SessionService::OnPlayerLevel(const NotifyPlayerLevel& acMessage) noexcept
{
    if (const auto it = m_players.find(acMessage.PlayerId); it != m_players.end())
        it.value().Level = acMessage.NewLevel;
}

void SessionService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_players.clear();
}
