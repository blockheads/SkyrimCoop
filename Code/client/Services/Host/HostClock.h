#pragma once

#include <DateTime.h>
#include <network/ENetInterface.h>

#include <optional>

struct HostAuthority;
struct HostGame;
struct HostPlayers;
struct SetTimeCommandRequest;

struct HostNetwork;

/**
 * @brief The session's time is the host's game clock. Peers run their own clock at its speed and
 * follow the host's when it jumps (sleep, wait, a set hour) or its speed changes.
 */
struct HostClock
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aAuthority.
    HostClock(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostClock);

    // Every frame: tells peers when the host's clock left the one they run.
    void Update(uint64_t aTick) noexcept;
    void SendTime(ConnectionId_t aPlayer) const noexcept;

private:
    // Any player's: sets the host's clock, which everyone else then follows.
    void OnSetTimeCommand(const SetTimeCommandRequest& acRequest, ConnectionId_t aSender) const noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostGame& m_game;
    // The clock peers run on, as of m_peersClockTick.
    std::optional<DateTime> m_peersClock;
    uint64_t m_peersClockTick{};
};
