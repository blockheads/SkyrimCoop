#pragma once

#include <ENetInterface.hpp>

struct HostBridge;
struct HostPlayers;
struct TimeScaleRequest;

namespace Server
{
struct GameServer;
}

/**
 * @brief One clock for everyone: a game slowing its own (a kill cam, Slow Time, a bow zoom) slows the
 * others. Each game says how fast it wants its clock; each is told the slowest of the others' and
 * runs no faster.
 */
struct HostTimeScale
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    HostTimeScale(HostBridge& aBridge, Server::GameServer& aServer, const HostPlayers& acPlayers) noexcept;

    TP_NOCOPYMOVE(HostTimeScale);

    void OnTimeScale(const TimeScaleRequest& acRequest, ConnectionId_t aSender) noexcept;
    // After the player is off the list: its slow clock no longer holds the others.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;
    // A player's character is in the world: it runs no faster than the others want.
    void SendTimeScale(ConnectionId_t aPlayer) noexcept;

private:
    [[nodiscard]] float SlowestBesides(ConnectionId_t aPlayer) const noexcept;
    // Tells each player whose others' slowest clock changed.
    void Tell() noexcept;
    void Tell(ConnectionId_t aPlayer, float aMultiplier) noexcept;

    Server::GameServer& m_server;
    const HostPlayers& m_players;
    TiltedPhoques::Map<ConnectionId_t, float> m_wanted; // each game's own clock
    TiltedPhoques::Map<ConnectionId_t, float> m_told;   // what each game was last told
};
