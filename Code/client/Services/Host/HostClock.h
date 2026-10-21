#pragma once

#include <DateTime.h>
#include <ENetInterface.hpp>

#include <optional>

struct HostAuthority;
struct HostGame;
struct HostPlayers;
struct SetTimeCommandRequest;

namespace Server
{
struct GameServer;
}

/**
 * @brief The session's time is the host's game clock. Peers run their own clock at its speed and
 * follow the host's when it jumps (sleep, wait, a set hour) or its speed changes.
 */
struct HostClock
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    // Registers its rules with aAuthority.
    HostClock(HostAuthority& aAuthority, Server::GameServer& aServer, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    TP_NOCOPYMOVE(HostClock);

    // Every frame: tells peers when the host's clock left the one they run.
    void Update(uint64_t aTick) noexcept;
    void SendTime(ConnectionId_t aPlayer) const noexcept;

private:
    // Any player's: sets the host's clock, which everyone else then follows.
    void OnSetTimeCommand(const SetTimeCommandRequest& acRequest, ConnectionId_t aSender) const noexcept;

    Server::GameServer& m_server;
    const HostPlayers& m_players;
    const HostGame& m_game;
    // The clock peers run on, as of m_peersClockTick.
    std::optional<DateTime> m_peersClock;
    uint64_t m_peersClockTick{};
};
