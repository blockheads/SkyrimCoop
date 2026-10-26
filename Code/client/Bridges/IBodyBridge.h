#pragma once

#include <Structs/BodyFrame.h>

#include <cstdint>
#include <functional>
#include <optional>

// The session's bodies on this game, by key (docs/design/physics-sync.md, "Owners"): a reference's, by its 3D's
// order, or a copy's ragdoll's. Every game plays the bodies other games own through it.
class IBodyBridge
{
public:
    virtual ~IBodyBridge() = default;

    // How a body's reference is found here: a guest's, the host's ids through its own (its twins of the host's
    // created references); none set, this game's own ids.
    virtual void SetReferences(std::function<uint32_t(const GameId& acId)> aFormIdOf) noexcept = 0;
    // A reference's body where this game shows it.
    [[nodiscard]] virtual std::optional<BodyPose> ReadPose(const BodyKey& acKey) noexcept = 0;
    // Put at the frame and held keyframed, moving so. A copy's ragdoll body (aCopyFormId) is taken only once its own
    // ragdoll simulates it, and kept (true while it waits). False: that body is not here.
    virtual bool Drive(const BodyFrame& acFrame, uint32_t aCopyFormId = 0) noexcept = 0;
    // Put at its rest, still: a reference's simulated again, asleep, so this game's player can push and grab it; a
    // copy's ragdoll body stays held.
    virtual bool Rest(const BodyPose& acPose, uint32_t aCopyFormId = 0) noexcept = 0;
    // A held reference's body simulated again from the frame, moving so (a guest's player let go of it).
    virtual bool Free(const BodyFrame& acFrame) noexcept = 0;
    // A player's character's impulse on a reference's body, at a point (Havok's character proxy).
    virtual bool Push(const BodyPush& acPush) noexcept = 0;
    // A reference this game holds (a world state row's rest keyframes its 3D): its body's own motion kept, false
    // for one that is not dynamic; then simulated again where it is, asleep.
    virtual bool KeepMotion(const BodyKey& acKey) noexcept = 0;
    virtual bool Settle(const BodyKey& acKey) noexcept = 0;
};
