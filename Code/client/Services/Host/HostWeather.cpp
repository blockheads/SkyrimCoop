#include <Services/Host/HostWeather.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/NotifyWeatherChange.h>
#include <Messages/WeatherRegionRequest.h>

HostWeather::HostWeather(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostWeather::OnRegion);
}

void HostWeather::Update() noexcept
{
    // Not before the host's own player is in: "everyone but the host" would include it.
    const ConnectionId_t cHost = m_players.Host();
    if (!cHost)
        return;
    // None for a while (an interior, the map): guests keep what they hold.
    const auto cSky = m_game.ReadWeather();
    if (!cSky || cSky == m_sky)
        return;
    m_sky = cSky;
    for (const ConnectionId_t cPlayer : m_players.Connections())
        if (cPlayer != cHost)
            SendFor(cPlayer);
}

void HostWeather::SendWeather(ConnectionId_t aPlayer) noexcept
{
    m_sent.erase(aPlayer);
    SendFor(aPlayer);
}

void HostWeather::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    m_regions.erase(aPlayer);
    m_sent.erase(aPlayer);
}

std::optional<GameId> HostWeather::WeatherFor(const GameId& acRegion) const noexcept
{
    if (!m_sky)
        return std::nullopt;
    // Forced weather wins everywhere; the host's region shows the host's own.
    if (m_sky->Forced || !acRegion || acRegion == m_sky->Region || !m_game.RegionWeather)
        return m_sky->Weather;
    return m_game.RegionWeather(acRegion).value_or(m_sky->Weather);
}

void HostWeather::OnRegion(const WeatherRegionRequest& acRequest, ConnectionId_t aSender) noexcept
{
    m_regions[aSender] = acRequest.Region;
    // A joiner gets its weather with the rest it must know (SendWeather).
    if (m_players.IsPlayer(aSender) && aSender != m_players.Host())
        SendFor(aSender);
}

void HostWeather::SendFor(ConnectionId_t aPlayer) noexcept
{
    const auto cRegion = m_regions.find(aPlayer);
    const auto cWeather = WeatherFor(cRegion == m_regions.end() ? GameId{} : cRegion->second);
    if (!cWeather)
        return;
    const auto cSent = m_sent.find(aPlayer);
    if (cSent != m_sent.end() && cSent->second == *cWeather)
        return;
    m_sent[aPlayer] = *cWeather;
    NotifyWeatherChange notify;
    notify.Id = *cWeather;
    m_network.Send(aPlayer, notify);
}
