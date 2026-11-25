#pragma once

#include <Bridges/GuestEngine.h>
#include <Physics/BodyAuthority.h>
#include <Physics/BodyMotions.h>
#include <Physics/BodyStream.h>
#include <Physics/PairedPoses.h>
#include <Services/BodyService.h>

#include <NoCopyMove.h>

#include <map>
#include <set>
#include <vector>

struct World;

/**
 * @brief This game's authority over the session's bodies (docs/design/physics-authority-plan.md, piece 1): what its player
 * moves, streamed to the host until the host confirms its rest.
 */
struct GuestBodies final : BodyService::Keeper
{
    // This game's player, as BodyAuthority numbers it here (the host's states name "you" or "another").
    static constexpr uint32_t kMe = 1;

    GuestBodies(World& aWorld, GuestEngine& aEngine) noexcept;
    ~GuestBodies() noexcept override;

    NO_COPY_MOVE(GuestBodies);

    [[nodiscard]] std::vector<BodyKey> Update(uint64_t aTick) noexcept override;
    [[nodiscard]] bool Hears(const BodyState& acState) noexcept override;
    [[nodiscard]] bool Simulates(const BodyKey& acKey) const noexcept override { return m_authority.Is(acKey, kMe); }

private:
    // A body no physics simulates here (a script holds it keyframed, or another game's hand): not taken by a touch.
    [[nodiscard]] bool HostsOnly(const BodyKey& acKey) const noexcept;
    // This game's player's touches, grabs and lets go; the bodies taken into aTaken.
    void TakeHands(uint64_t aTick, std::map<BodyKey, BodyFrame>& aHeld, std::vector<BodyKey>& aTaken) noexcept;
    // Its player's ragdoll: its frames into aStates while it moves, its rest declared.
    void Ragdoll(uint64_t aTick, GuestEngine::OwnBodies& aOwn, std::vector<BodyState>& aStates) noexcept;
    // No longer this game's: the host's states show it from now.
    void Drop(const BodyKey& acKey) noexcept;
    // The host's newest state of it is too far off to steer to: it is not where this game still shows it.
    [[nodiscard]] bool Elsewhere(const BodyKey& acKey) const noexcept;
    void Took(const BodyKey& acKey, const char* acpHow, std::vector<BodyKey>& aTaken) noexcept;
    [[nodiscard]] GuestEngine::SessionOf_t CopySessions() const noexcept;
    [[nodiscard]] uint32_t PlayerId() const noexcept;

    World& m_world;
    GuestEngine& m_engine;
    BodyAuthority m_authority;
    BodyStream m_stream;
    BodyMotions m_motions;
    PairedPoses m_paired; // what Havok pairs with its bodies, and where each lay
    std::set<BodyKey> m_held;               // in this game's player's hands
    std::map<BodyKey, BodyPose> m_rests;    // its rests, until the host confirms them
    std::map<BodyKey, BodyPose> m_last;     // its bodies where this game's physics last read them
    std::map<BodyKey, BodyState> m_confirmed; // its rests the host confirmed: lying here already
    std::set<BodyKey> m_heard;              // bodies the host's stream has carried
    std::map<BodyKey, BodyPose> m_hostRests; // the host's last rest of each, as heard
    std::map<BodyKey, std::pair<uint64_t, BodyPose>> m_hinted; // moved here alone, the host told then (again each kHintMs until heard)
    std::map<BodyKey, glm::vec3> m_newest; // where the host's newest state has each
    static constexpr uint64_t kHintMs = 1000;
    uint32_t m_ragdollFrames{};             // the ragdoll's frames read since it woke (logged)
};
