#pragma once

#include <network/ENetInterface.h>

#include <Messages/RelayedMessages.h>
#include <Structs/BodyFrame.h>

#include <map>
#include <NoCopyMove.h>

struct HostAuthority;
struct HostCharacters;
struct HostGame;
struct HostPlayers;

/**
 * @brief A guest's own bodies: its player's hand on the host's, and its player's ragdoll (docs/design/physics-sync.md,
 * "Owners"):
 *
 *   push       the guest's character's impulse, given to the host's body alike
 *   hold       the body is the holder's while it holds it: its frames go to every other game, the host's too, which
 *              play them (BodyService); the first holder keeps it
 *   let go     the host's game simulates it again from the holder's last frame (also when the holder leaves, or
 *              sends no frame for kSilentMs)
 *   ragdoll    its own character's (the sender owns it): its frames and rests to every other game, the host's too
 */
struct HostHands
{
    using ConnectionId_t = Net::ConnectionId_t;

    // A holder whose frames stop this long (its game stalled, or a lost let go) drops what it held where it was.
    static constexpr uint64_t kSilentMs = 2000;

    HostHands(HostAuthority& aAuthority, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostHands);

    void Update(uint64_t aTick) noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    void OnPushes(const BodyPushRequest& acMessage, ConnectionId_t aSender) noexcept;
    void OnHeld(const BodyFramesRequest& acMessage, ConnectionId_t aSender) noexcept;
    void OnReleased(const BodyReleaseRequest& acMessage, ConnectionId_t aSender) noexcept;
    void OnRests(const BodyRestsRequest& acMessage, ConnectionId_t aSender) noexcept;
    // A held body's frame taken from its holder (the first holder keeps it).
    [[nodiscard]] bool TakeHeld(const BodyFrame& acFrame, uint64_t aTick, ConnectionId_t aSender) noexcept;
    // An actor's ragdoll body the sender's game owns.
    [[nodiscard]] bool OwnsRagdoll(const BodyKey& acKey, ConnectionId_t aSender) const noexcept;
    // Simulated again where the holder left it.
    void Free(const BodyFrame& acFrame) noexcept;

    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
    struct Hold
    {
        ConnectionId_t Holder{};
        BodyFrame Last{}; // its last frame
        uint64_t At{};    // when that came
    };
    std::map<BodyKey, uint64_t> m_pushed;   // a guest's last push of each (logged once a run)
    std::map<BodyKey, Hold> m_holds;        // held by a guest's player
    std::map<BodyKey, uint64_t> m_released; // let go at that tick: older frames of it come late
    std::map<BodyKey, uint32_t> m_ragdolls; // a guest's ragdoll bodies moving: frames forwarded (logged)
    uint64_t m_tick{};
};
