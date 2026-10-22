#pragma once

#include <network/ENetInterface.h>
#include <Structs/GameId.h>
#include <NoCopyMove.h>

struct HostGame;
struct HostPlayers;

struct HostNetwork;

/**
 * @brief The session's weather is the host's. Peers hold the host's weather in place of their
 * own and change it when the host's changes.
 */
struct HostWeather
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostWeather(HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostWeather);

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
