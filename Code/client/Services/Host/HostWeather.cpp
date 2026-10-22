#include <Services/Host/HostWeather.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/NotifyWeatherChange.h>

HostWeather::HostWeather(HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_game(acGame)
{
}

void HostWeather::Update() noexcept
{
    // Not before the host's own player is in: "everyone but the host" would include it.
    const ConnectionId_t cHost = m_players.Host();
    const auto cWeather = m_game.ReadWeather();
    if (!cHost || !cWeather || *cWeather == m_peersWeather)
        return;

    m_peersWeather = *cWeather;
    NotifyWeatherChange notify;
    notify.Id = m_peersWeather;
    m_players.SendToOthers(notify, cHost, TiltedPhoques::kReliable);
}

void HostWeather::SendWeather(ConnectionId_t aPlayer) const noexcept
{
    if (!m_peersWeather)
        return;

    NotifyWeatherChange notify;
    notify.Id = m_peersWeather;
    m_network.Send(aPlayer, notify);
}
