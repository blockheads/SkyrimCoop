#pragma once

#include <Structs/BodyFrame.h>

#include <cstdint>
#include <functional>
#include <optional>

// The session's bodies on this game, by key (docs/design/physics-sync.md, "Owners"): a reference's, by its 3D's
// order, or a copy's ragdoll's. Every game plays the bodies other games own through it.
// Who simulates a reference's body, which decides what it meets here (Physics/BodyGroups.h).
enum class BodyClass : uint8_t
{
    kNone,   // nobody moves it
    kPlayed, // another game: steered here
    kMoving, // this game
};

class IBodyBridge
{
public:
    virtual ~IBodyBridge() = default;

    // How a body's reference is found here: a guest's, the host's ids through its own (its twins of the host's
    // created references); none set, this game's own ids.
    virtual void SetReferences(std::function<uint32_t(const GameId& acId)> aFormIdOf) noexcept = 0;
    // A reference's body where this game shows it.
    [[nodiscard]] virtual std::optional<BodyPose> ReadPose(const BodyKey& acKey) noexcept = 0;
    // Steered onto the frame, simulated; aHeld (held in another game): keyframed, so this game's grab refuses it. A
    // copy's ragdoll body waits for its own ragdoll (true meanwhile). False: not here (physics-sync.md, "One rule").
    virtual bool Drive(const BodyFrame& acFrame, uint32_t aCopyFormId = 0, bool aHeld = false) noexcept = 0;
    // A living copy standing goes down now, on its own ragdoll (its owner's frames then steer it). False: not here.
    virtual bool KnockDown(uint32_t aCopyFormId) noexcept = 0;
    // The copies in this game, each update: a copy's capsule pushes no object (its owner's game moves what its
    // character walks into).
    virtual void SetCopies(const std::vector<uint32_t>& acCopyFormIds) noexcept = 0;
    // This game moves that reference's body, or no longer (kNone); ignored while another game's frames play it
    // (Drive marks it played, its Rest or Forget ends that).
    virtual void SetMoving(const BodyKey& acKey, bool aMoving) noexcept = 0;
    // Another game's frames of it ended without a rest (taken over here, lost): nobody's.
    virtual void Forget(const BodyKey& acKey) noexcept = 0;
    // The session ended: every body as the engine had it.
    virtual void ForgetAll() noexcept = 0;
    // A reference's body's form id in this game, 0 for an actor's or one not here.
    [[nodiscard]] virtual uint32_t ReferenceFormId(const BodyKey& acKey) const noexcept = 0;
    // Put at its rest, still: a reference's simulated again, asleep, so this game's player can push and grab it; a
    // copy's ragdoll remains simulated and follows its owner's rest.
    virtual bool Rest(const BodyPose& acPose, uint32_t aCopyFormId = 0) noexcept = 0;
    // A released reference's body simulated from its last held frame, even if its held packets were lost.
    virtual bool Free(const BodyFrame& acFrame) noexcept = 0;
    // A reference this game holds (a world state row's rest keyframes its 3D): its body's own motion kept, false
    // for one that is not dynamic; then simulated again where it is, asleep.
    virtual bool KeepMotion(const BodyKey& acKey) noexcept = 0;
    virtual bool Settle(const BodyKey& acKey) noexcept = 0;
};
