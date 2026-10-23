#include <Services/SessionService.h>

#include <Events/DisconnectedEvent.h>

#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerLevel.h>

SessionService::SessionService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
{
    Listen<NotifyPlayerJoined, &SessionService::OnPlayerJoined>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerLeft, &SessionService::OnPlayerLeft>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerLevel, &SessionService::OnPlayerLevel>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &SessionService::OnDisconnected>(m_connections, aDispatcher, this);
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
        it->second.Level = acMessage.NewLevel;
}

void SessionService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_players.clear();
}
