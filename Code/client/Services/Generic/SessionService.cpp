#include <Services/SessionService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/TransportService.h>
#include <World.h>
#include <Events/SessionNoticeEvent.h>

#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/Relayed.h>
#include <Messages/DeathMessages.h>
#include <Diagnostics/Profile.h>

SessionService::SessionService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher)
{
    Listen<MenuEvent, &SessionService::OnMenu>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &SessionService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<ConnectedEvent, &SessionService::OnConnected>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerJoined, &SessionService::OnPlayerJoined>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerLeft, &SessionService::OnPlayerLeft>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerLevel, &SessionService::OnPlayerLevel>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerName, &SessionService::OnPlayerName>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerPerished, &SessionService::OnPlayerPerished>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &SessionService::OnDisconnected>(m_connections, aDispatcher, this);
}

void SessionService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    spdlog::info("[SessionService] {} ({}) joined{}", acMessage.Username.c_str(), acMessage.PlayerId, acMessage.IsHost ? ", the host" : "");
    m_players[acMessage.PlayerId] = {acMessage.Username, acMessage.IsHost, acMessage.Level};
    if (acMessage.Arrived)
        m_dispatcher.trigger(SessionNoticeEvent{acMessage.Username + " joined the party."});
}

void SessionService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    spdlog::info("[SessionService] {} ({}) left", acMessage.Username.c_str(), acMessage.PlayerId);
    m_players.erase(acMessage.PlayerId);
    m_dispatcher.trigger(SessionNoticeEvent{acMessage.Username + " left the party."});
}

void SessionService::OnPlayerPerished(const NotifyPlayerPerished& acMessage) const noexcept
{
    m_dispatcher.trigger(SessionNoticeEvent{acMessage.KillerName.empty() ? acMessage.Username + " has perished."
                                                                         : acMessage.Username + " was killed by " + acMessage.KillerName + ".",
                                            true});
}

void SessionService::OnPlayerLevel(const NotifyPlayerLevel& acMessage) noexcept
{
    if (const auto it = m_players.find(acMessage.PlayerId); it != m_players.end())
        it->second.Level = acMessage.NewLevel;
}

void SessionService::OnPlayerName(const NotifyPlayerName& acMessage) noexcept
{
    if (const auto it = m_players.find(acMessage.PlayerId); it != m_players.end())
        it->second.Name = acMessage.Name;
}

void SessionService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_players.clear();
}

void SessionService::OnConnected(const ConnectedEvent&) noexcept
{
    m_ending = m_finished = m_pending = false;
    m_endReason.clear();
}

void SessionService::End(std::string aReason) noexcept
{
    if (m_ending)
        return;
    m_ending = m_pending = true;
    m_endReason = std::move(aReason);
}

std::string SessionService::NameOf(uint32_t aPlayerId) const noexcept
{
    const auto& cTransport = m_world.GetTransport();
    if (aPlayerId == cTransport.GetLocalPlayerId())
        return cTransport.GetLocalPlayerName();
    const auto cPlayer = m_players.find(aPlayerId);
    return cPlayer != m_players.end() ? cPlayer->second.Name : std::string("?");
}

void SessionService::QuitToMainMenu() noexcept
{
    if (!m_ending)
        m_endReason.clear();
    m_ending = true;
    Finish(false);
}

void SessionService::OnMenu(const MenuEvent& acEvent) noexcept
{
    if (acEvent.Opening && acEvent.Menu == "Main Menu" &&
        (m_world.GetTransport().IsConnected() || m_world.IsHosting() ||
         (m_end.IsJoining && m_end.IsJoining())))
        QuitToMainMenu();
}

void SessionService::OnUpdate(const UpdateEvent&) noexcept
{
    PROFILE_ZONE("SessionService::OnUpdate");
    if (m_pending)
        Finish(true);
}

void SessionService::Finish(bool aReturnToMenu) noexcept
{
    if (m_finished)
        return;
    m_finished = true;
    m_pending = false;
    if (m_end.Finish)
        m_end.Finish(m_endReason, aReturnToMenu);
}
