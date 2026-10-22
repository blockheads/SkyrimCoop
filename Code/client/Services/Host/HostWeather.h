#pragma once

#include <ENetInterface.hpp>
#include <Structs/GameId.h>

struct HostGame;
struct HostPlayers;

struct HostNetwork;

/**
 * @brief The session's weather is the host's. Peers hold the host's weather in place of their
 * own and change it when the host's changes.
 */
struct HostWeather
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    HostWeather(HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    TP_NOCOPYMOVE(HostWeather);

    // Every frame: tells peers when the host's weather changed.
    void Update() noexcept;
    void SendWeather(ConnectionId_t aPlayer) const noexcept;

private:
    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostGame& m_game;
    // What peers hold (none yet: the host has shown no weather to share).
    GameId m_peersWeather{};
};
