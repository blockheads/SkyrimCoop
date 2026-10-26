#pragma once

#include <Physics/BodySends.h>

#include <NoCopyMove.h>

#include <map>
#include <set>
#include <vector>

struct World;
class GuestEngine;

/**
 * @brief This game's player's hand on the host's bodies, and this game's own physics (docs/design/physics-sync.md,
 * "A guest's own actions"):
 *
 *   push, hold    the body is this game's own until the host's rest: its own physics shows it, the pushes and the
 *                 held frames go to the host (then a let go), and the host's frames of it wait
 *   moved alone   a body this game's physics moved by itself (its copy of a blast, a bump of its own body) that the
 *                 host's frames never follow asks the host where it is, kOwnMs later
 *
 * GuestPhysics plays everything else from the host's frames, and asks this which bodies are this game's.
 */
struct GuestHands
{
    // A body this game moved this long ago whose move the host never followed asks the host where it is (the host's
    // first frame comes back within a round trip).
    static constexpr uint64_t kOwnMs = 1000;

    GuestHands(World& aWorld, GuestEngine& aEngine) noexcept;

    NO_COPY_MOVE(GuestHands);

    // Each update: this game's player's pushes, holds and let gos to the host, and the host asked about what this
    // game moved alone. The bodies it made this game's own this update (the host's playback of them stops); acPlayed:
    // the bodies the host's frames move now.
    [[nodiscard]] std::vector<BodyKey> Update(uint64_t aTick, const std::set<BodyKey>& acPlayed) noexcept;
    // A host frame of that body: false while it is this game's own (the frame waits; the host did move it).
    [[nodiscard]] bool TakesHostFrame(const BodyKey& acKey) noexcept;
    // Its rest is not the host's to place while this game's player holds it.
    [[nodiscard]] bool IsHeld(const BodyKey& acKey) const noexcept { return m_holding.contains(acKey); }
    // The host's rest placed: the host's again.
    void OnHostRest(const BodyPose& acRest) noexcept;

private:
    struct Owned
    {
        uint64_t Touched{}; // this game's player's last push or hold
        bool HostMoved{};   // the host sent frames of it since
    };
    struct Moved
    {
        uint64_t Since{};
        BodyPose From{}; // where the host last put it (or where it woke)
        bool Far{};      // past the host's send rule's error from there: not a settle in place
        bool Seen{};     // awake this update
    };

    // This game's player's pushes and holds to the host; the bodies it newly owns into aOwned.
    void SendHands(uint64_t aTick, std::vector<BodyKey>& aOwned) noexcept;
    void Own(const BodyKey& acKey, uint64_t aTick, const char* acpHow, std::vector<BodyKey>& aOwned) noexcept;
    void TrackMoved(uint64_t aTick, const std::set<BodyKey>& acPlayed) noexcept;
    // Own bodies the host never moved, and bodies moved alone: the host's poses asked for.
    void AskHost(uint64_t aTick) noexcept;

    World& m_world;
    GuestEngine& m_engine;
    std::map<BodyKey, Owned> m_own;          // this game's player's, until the host's rest
    std::map<BodyKey, BodyFrame> m_holding;  // in this game's player's hands: its last frame
    BodySends m_holdSends;                   // the held frames the host would guess wrong
    std::map<BodyKey, Moved> m_moved;        // moved by this game's physics alone
    std::map<BodyKey, BodyPose> m_hostRests; // where the host's rests put them
};
