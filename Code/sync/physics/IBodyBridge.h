#pragma once

#include <physics/BodyRead.h>
#include <Structs/BodyFrame.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// The session's bodies on this game, by key (docs/design/physics-authority-plan.md): a reference's, by its 3D's
// order, or a copy's ragdoll's. Every game shows the bodies it does not simulate through it, and the rest placer
// (RestPlacer, in BodyService) puts another game's rest on a body through its steps.
// Who simulates a reference's body, which decides what it meets here (sync/physics/BodyGroups.h).
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
    // aIdOf: the other way, a form id here to the session's id (none: not shared).
    virtual void SetReferences(std::function<uint32_t(const GameId& acId)> aFormIdOf, std::function<std::optional<GameId>(uint32_t aFormId)> aIdOf) noexcept = 0;
    // A reference's body as this game has it now; none when it is not here.
    [[nodiscard]] virtual std::optional<BodyRead> Read(const BodyKey& acKey) noexcept = 0;
    // The dynamic reference bodies Havok paired with each of acKeys (its collision entries), as pairs.
    [[nodiscard]] virtual std::vector<std::pair<BodyKey, BodyKey>> ReadTouching(const std::vector<BodyKey>& acKeys) noexcept = 0;
    // Steered onto the frame, simulated; aHeld (held in another game): keyframed, so this game's grab refuses it. A
    // copy's ragdoll body waits for its own ragdoll (true meanwhile). False: not here.
    virtual bool Drive(const BodyFrame& acFrame, uint32_t aCopyFormId = 0, bool aHeld = false) noexcept = 0;
    // A rest's steps (RestPlacer), a reference's body: steered onto it; put there awake and still; its island put to
    // sleep if all of it is still and none this game moves or holds (true: asleep); held there and simulated again
    // asleep. False: not here.
    virtual bool Glide(const BodyPose& acRest) noexcept = 0;
    virtual bool Place(const BodyPose& acRest) noexcept = 0;
    virtual bool Sleep(const BodyKey& acKey) noexcept = 0;
    virtual bool HoldAt(const BodyPose& acRest) noexcept = 0;
    // A hold here ended with no state after it (its authority gone): its own motion back, simulated where it is.
    virtual bool Release(const BodyKey& acKey) noexcept = 0;
    // A copy's ragdoll at its owner's rest (CopyRagdollPhysics places it as one). False: not here.
    virtual bool RestRagdoll(const BodyPose& acRest, uint32_t aCopyFormId) noexcept = 0;
    // A living copy standing goes down now, on its own ragdoll (its owner's frames then steer it). False: not here.
    virtual bool KnockDown(uint32_t aCopyFormId) noexcept = 0;
    // The copies in this game, each update: a copy's capsule pushes no object (its owner's game moves what its
    // character walks into).
    virtual void SetCopies(const std::vector<uint32_t>& acCopyFormIds) noexcept = 0;
    // This game moves that reference's body, or no longer (kNone); ignored while another game's frames play it
    // (Drive marks it played, Forget ends that).
    virtual void SetMoving(const BodyKey& acKey, bool aMoving) noexcept = 0;
    // Another game's frames of it ended (taken here, at rest, lost): nobody's.
    virtual void Forget(const BodyKey& acKey) noexcept = 0;
    // The session ended: every body as the engine had it.
    virtual void ForgetAll() noexcept = 0;
    // A reference's body's form id in this game, 0 for an actor's or one not here.
    [[nodiscard]] virtual uint32_t ReferenceFormId(const BodyKey& acKey) const noexcept = 0;
    // A reference a world state row places (a cell's load): its body's own motion kept, false for one that is not
    // dynamic; then simulated again where it is, asleep.
    virtual bool KeepMotion(const BodyKey& acKey) noexcept = 0;
    virtual bool Settle(const BodyKey& acKey) noexcept = 0;
};
