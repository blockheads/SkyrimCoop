#pragma once

#include <network/ENetInterface.h>
#include <Services/Host/HostTimeGame.h>
#include <Structs/GameId.h>
#include <NoCopyMove.h>

#include <optional>
#include <unordered_map>

struct HostRouter;
struct HostPlayers;
struct HostNetwork;
struct WeatherRegionRequest;

/**
 * @brief The session's weather is the host's, per region (docs/re/systems/weather.md): each guest holds the weather the
 * host's game gives the guest's region, the host's own where it is forced or in the host's region.
 */
struct HostWeather
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostWeather(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostTimeGame& acGame) noexcept;

    NO_COPY_MOVE(HostWeather);

    // Every frame: tells each guest when the weather for its region changed.
    void Update() noexcept;
    void SendWeather(ConnectionId_t aPlayer) noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

    // The weather a guest in aRegion holds; none: the host shows none to share.
    [[nodiscard]] std::optional<GameId> WeatherFor(const GameId& acRegion) const noexcept;

private:
    void OnRegion(const WeatherRegionRequest& acRequest, ConnectionId_t aSender) noexcept;
    void SendFor(ConnectionId_t aPlayer) noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostTimeGame& m_game;
    std::optional<HostSky> m_sky;
    // Each guest's weather region as it last reported it, and the weather it was sent.
    std::unordered_map<ConnectionId_t, GameId> m_regions;
    std::unordered_map<ConnectionId_t, GameId> m_sent;
};
