#include <Services/Host/HostWeather.h>

#include <GameServer.h>
#include <Services/Host/HostGame.h>

#include <Messages/NotifyWeatherChange.h>

HostWeather::HostWeather(Server::GameServer& aServer, const HostGame& acGame) noexcept
    : m_server(aServer)
    , m_game(acGame)
{
}

void HostWeather::Update() noexcept
{
    const auto cWeather = m_game.ReadWeather();
    if (!cWeather || *cWeather == m_peersWeather)
        return;

    m_peersWeather = *cWeather;
    NotifyWeatherChange notify;
    notify.Id = m_peersWeather;
    m_server.SendToOthers(notify, m_server.HostConnection(), TiltedPhoques::kReliable);
}

void HostWeather::SendWeather(ConnectionId_t aPlayer) const noexcept
{
    if (!m_peersWeather)
        return;

    NotifyWeatherChange notify;
    notify.Id = m_peersWeather;
    m_server.Send(aPlayer, notify);
}
