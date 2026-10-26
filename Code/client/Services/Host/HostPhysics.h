#pragma once

#include <network/ENetInterface.h>

#include <Messages/RelayedMessages.h>
#include <Physics/BodySends.h>

#include <map>
#include <NoCopyMove.h>

struct HostAuthority;
struct HostGame;
struct HostNetwork;
struct HostPlayers;

/**
 * @brief The host's game simulates every body; a guest shows its result (docs/design/physics-sync.md):
 *
 *   each update   each guest gets the frames of the awake bodies near its copy that it would guess wrong, within
 *                 its budget (unreliable); everyone the bodies that fell asleep, where they rest (reliable)
 *   asked         a guest whose own physics moved a body the host's did not gets the host's pose back
 *
 * A guest's player's hand on the bodies is HostHands'.
 */
struct HostPhysics
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostPhysics(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostPhysics);

    void Update(uint64_t aTick) noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    void OnRestAsked(const BodyRestRequest& acMessage, ConnectionId_t aSender) noexcept;
    void SendBodies(uint64_t aTick) noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostGame& m_game;
    BodyMotions m_motions;
    std::map<ConnectionId_t, BodySends> m_sends; // each guest's: what it got, its budget
    std::map<ConnectionId_t, std::map<BodyKey, uint32_t>> m_sentFrames; // each guest's bodies since they woke: frames sent (logged)
    uint64_t m_lastTick{};
};
