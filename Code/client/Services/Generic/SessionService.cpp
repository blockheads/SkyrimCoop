#include <Services/SessionService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/HostService.h>
#include <Services/JoinService.h>
#include <Services/SteamService.h>
#include <Services/TransportService.h>
#include <World.h>
#include <Events/SessionNoticeEvent.h>

#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/DeathMessages.h>

SessionService::SessionService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher)
{
    Listen<MenuEvent, &SessionService::OnMenu>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &SessionService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<ConnectedEvent, &SessionService::OnConnected>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerJoined, &SessionService::OnPlayerJoined>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerLeft, &SessionService::OnPlayerLeft>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerLevel, &SessionService::OnPlayerLevel>(m_connections, aDispatcher, this);
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
        (m_world.GetTransport().IsConnected() || m_world.GetHostService().IsHosting() ||
         m_world.ctx().at<JoinService>().IsJoining()))
        QuitToMainMenu();
}

void SessionService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_pending)
        Finish(true);
}

void SessionService::Finish(bool aReturnToMenu) noexcept
{
    if (m_finished)
        return;
    m_finished = true;
    m_pending = false;
    auto& host = m_world.GetHostService();
    auto& join = m_world.ctx().at<JoinService>();
    // The final character record needs the Steam identity until the transport closes.
    if (host.IsHosting())
        host.AnnounceEnd();
    m_world.GetTransport().Close();
    host.StopHosting();
    auto& steam = m_world.ctx().at<SteamService>();
    steam.EndRejoin();
    steam.Leave();
    join.Cancel(m_endReason);
    auto& bridge = m_world.ctx().at<IJoinBridge&>();
    if (aReturnToMenu && !bridge.IsAtMainMenu())
        bridge.ReturnToMainMenu();
}
