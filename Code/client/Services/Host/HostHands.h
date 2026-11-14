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
 *   hold       the holder's alone: its frames go to every other game, the host's too, which play them keyframed
 *              (BodyService), so nobody else grabs or pushes it; its own game's physics, copies included, has the
 *              last word (docs/re/systems/actor-movement.md, "Copies step"); the first holder keeps it
 *   let go     the host's game simulates it again from the holder's last frame (also when the holder leaves, or
 *              sends no frame for kSilentMs), still the mover's: its free frames steer every other game's
 *   moved      pushed, hit, let go or dropped: the mover's until it rests in the mover's game ("The mover's until
 *              its rest"): its free frames go to every other game, the host's too, played steered; its rest placed
 *              everywhere; the host's own frames and rests of it are not sent meanwhile (HostPhysics)
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

    // A guest's player holds or moved that body, or its rest is still reaching the host's game: its frames and rest
    // are that game's, not the host's.
    [[nodiscard]] bool MovedByGuest(const BodyKey& acKey) const noexcept { return m_holds.contains(acKey) || m_guestRests.contains(acKey); }
    // The host's own rest of a body a guest moved: false if it is that guest's rest (within the send rule's error),
    // sent already; true (the host's to send) if the host's game moved it on since. Either way the host's again.
    [[nodiscard]] bool HostRests(const BodyPose& acRest) noexcept;

private:
    void OnPushes(const BodyPushRequest& acMessage, ConnectionId_t aSender) noexcept;
    void OnHeld(const BodyFramesRequest& acMessage, ConnectionId_t aSender) noexcept;
    void OnReleased(const BodyReleaseRequest& acMessage, ConnectionId_t aSender) noexcept;
    void OnRests(const BodyRestsRequest& acMessage, ConnectionId_t aSender) noexcept;
    // A held or moved body's frame taken from its mover (the first keeps it).
    [[nodiscard]] bool TakeHeld(const BodyFrame& acFrame, uint64_t aTick, ConnectionId_t aSender, bool aHeld) noexcept;
    // An actor's ragdoll body the sender's game owns.
    [[nodiscard]] bool OwnsRagdoll(const BodyKey& acKey, ConnectionId_t aSender) const noexcept;
    // Simulated again where the holder left it.
    void Free(const BodyFrame& acFrame, uint64_t aReleasedAt = 0) noexcept;

    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
    struct Hold
    {
        ConnectionId_t Holder{};
        BodyFrame Last{}; // its last frame
        uint64_t Tick{};  // the holder's latest frame, to reject an older release
        uint64_t At{};    // when that came
        bool Held{};      // in its hands (keyframed elsewhere); else moving free until its rest
    };
    std::map<BodyKey, uint64_t> m_pushed;   // a guest's last push of each (logged once a run)
    std::map<BodyKey, Hold> m_holds;        // held or moved by a guest's player
    struct GuestRest
    {
        BodyPose Pose{};
        uint64_t At{};
    };
    std::map<BodyKey, GuestRest> m_guestRests; // a mover's rest, until the host's game has it at rest too
    std::map<BodyKey, uint64_t> m_released; // let go at that tick: older frames of it come late
    std::map<BodyKey, uint32_t> m_ragdolls; // a guest's ragdoll bodies moving: frames forwarded (logged)
    uint64_t m_tick{};
};
