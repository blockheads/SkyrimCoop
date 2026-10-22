#pragma once

#include <network/ENetInterface.h>

struct HostAuthority;
struct HostPlayers;
struct TimeScaleRequest;

struct HostNetwork;

/**
 * @brief One clock for everyone: a game slowing its own (a kill cam, Slow Time, a bow zoom) slows the
 * others. Each game says how fast it wants its clock; each is told the slowest of the others' and
 * runs no faster.
 */
struct HostTimeScale
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostTimeScale(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers) noexcept;

    NO_COPY_MOVE(HostTimeScale);

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

    HostNetwork& m_network;
    const HostPlayers& m_players;
    std::unordered_map<ConnectionId_t, float> m_wanted; // each game's own clock
    std::unordered_map<ConnectionId_t, float> m_told;   // what each game was last told
};
