#pragma once

#include <network/ENetInterface.h>

#include <Messages/RelayedMessages.h>
#include <Physics/BodyPlayback.h>

#include <map>
#include <NoCopyMove.h>

struct HostAuthority;
struct HostGame;
struct HostNetwork;
struct HostPlayers;

/**
 * @brief A guest's player's hand on the host's bodies (docs/design/physics-sync.md, "A guest's own actions"):
 *
 *   push       the guest's character's impulse, given to the host's body alike
 *   hold       the holder's frames, played kHeldDelayMs behind (the body keyframed there); the other guests get
 *              the holder's frames too; the first holder keeps it
 *   let go     simulated again from the holder's last frame (also when the holder leaves or goes silent)
 */
struct HostHands
{
    using ConnectionId_t = Net::ConnectionId_t;

    // A held body plays the holder's frames this far behind (as a guest plays the host's).
    static constexpr uint64_t kHeldDelayMs = 100;

    HostHands(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostHands);

    void Update(uint64_t aTick) noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    void OnPushes(const BodyPushRequest& acMessage, ConnectionId_t aSender) noexcept;
    void OnHeld(const BodyFramesRequest& acMessage, ConnectionId_t aSender) noexcept;
    void OnReleased(const BodyReleaseRequest& acMessage, ConnectionId_t aSender) noexcept;
    // Simulated again where the holder left it.
    void Free(const BodyFrame& acFrame) noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostGame& m_game;
    std::map<BodyKey, uint64_t> m_pushed;        // a guest's last push of each (logged once a run)
    std::map<BodyKey, ConnectionId_t> m_holders; // held by a guest's player
    std::map<BodyKey, BodyFrame> m_lastHeld;     // the holder's last frame of each
    std::map<BodyKey, uint64_t> m_released;      // let go at that tick: older frames of it come late
    BodyPlayback m_heldPlayback;                 // the holders' frames
    uint64_t m_tick{};
};
