#pragma once

#include <Bridges/GuestEngine.h>
#include <Physics/BodySends.h>
#include <Services/BodyService.h>

#include <NoCopyMove.h>

#include <map>
#include <set>
#include <vector>

struct World;

/**
 * @brief This game's own bodies: its player's hand on the host's, its player's ragdoll, and this game's own physics
 * (docs/design/physics-sync.md, "A guest's own actions", "Owners"):
 *
 *   push, hold    the body is this game's own until the host's rest: its own physics shows it, the pushes and the
 *                 held frames go to the host (then a let go), and the host's frames of it wait
 *   ragdoll       its player's while it simulates (dead): its frames, then its rest, to the host for the others
 *   moved alone   a body this game's physics moved by itself (its copy of a blast, a bump of its own body) that the
 *                 host's frames never follow asks the host where it is, kOwnMs later
 *
 * BodyService plays everything else from the host's frames, and asks this which bodies are this game's.
 */
struct GuestHands final : BodyService::Keeper
{
    // A body this game moved this long ago whose move the host never followed asks the host where it is (the host's
    // first frame comes back within a round trip).
    static constexpr uint64_t kOwnMs = 1000;

    GuestHands(World& aWorld, GuestEngine& aEngine) noexcept;
    ~GuestHands() noexcept override;

    NO_COPY_MOVE(GuestHands);

    // Each update: this game's player's pushes, holds and let gos to the host, and the host asked about what this
    // game moved alone. The bodies it made this game's own this update (the host's playback of them stops); acPlayed:
    // the bodies the host's frames move now.
    [[nodiscard]] std::vector<BodyKey> Update(uint64_t aTick, const std::set<BodyKey>& acPlayed) noexcept override;
    // A host frame of that body: false while it is this game's own (the frame waits; the host did move it).
    [[nodiscard]] bool TakesFrame(const BodyKey& acKey) noexcept override;
    // Its rest is not the host's to place while this game's player holds it.
    [[nodiscard]] bool KeepsRest(const BodyKey& acKey) const noexcept override { return m_holding.contains(acKey); }
    // The host's rest placed: the host's again.
    void OnRest(const BodyPose& acRest) noexcept override;

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

    // This game's player's pushes and let gos to the host; the bodies it newly owns into aOwned, the held ones' frames
    // into aFrames.
    void SendHands(uint64_t aTick, std::vector<BodyKey>& aOwned, std::vector<BodyFrame>& aFrames) noexcept;
    // This game's player's ragdoll: its awake bodies' frames into aFrames, its rest to the host.
    void SendRagdoll(uint64_t aTick, GuestEngine::OwnBodies& aOwn, std::vector<BodyFrame>& aFrames) noexcept;
    // This game's player's session id (its LocalComponent), 0 before it has one.
    [[nodiscard]] uint32_t PlayerId() const noexcept;
    void Own(const BodyKey& acKey, uint64_t aTick, const char* acpHow, std::vector<BodyKey>& aOwned) noexcept;
    void TrackMoved(uint64_t aTick, const std::set<BodyKey>& acPlayed, const std::vector<BodyPose>& acMoved) noexcept;
    // Own bodies the host never moved, and bodies moved alone: the host's poses asked for.
    void AskHost(uint64_t aTick) noexcept;

    World& m_world;
    GuestEngine& m_engine;
    std::map<BodyKey, Owned> m_own;          // this game's player's, until the host's rest
    std::map<BodyKey, BodyFrame> m_holding;  // in this game's player's hands: its last frame
    BodySends m_sends;                       // the held and ragdoll frames the others would guess wrong
    BodyMotions m_motions;                   // the ragdoll's free fall
    uint32_t m_ragdollFrames{};              // the ragdoll's frames read since it woke (logged)
    std::map<BodyKey, Moved> m_moved;        // moved by this game's physics alone
    std::map<BodyKey, BodyPose> m_hostRests; // where the host's rests put them
};
