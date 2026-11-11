#pragma once

#include <Structs/BodyFrame.h>

// What every game guesses a body does after a frame (docs/design/physics-sync.md, "Sent when the guess goes
// wrong"): the host sends a body again only where the guests' guess would be wrong. No engine: ClientTests.
namespace BodyPrediction
{
// A copy's body this far from its frame is placed, not steered. Its first frame only when well off: a kill move's
// victim starts within a few units of its owner, and a teleport there shows as a one-frame pop (kill-moves.md).
constexpr float PlaceDistance(bool aFirstFrame) noexcept
{
    return aFirstFrame ? 16.f : 64.f;
}

// Vanilla Havok's gravity (bhkWorld's hkpWorld::gravity, -9.8 m/s²) in game units/s².
constexpr float kGravity = -9.8f / 0.0142875f;
// Past this long a frame's guess stops moving: a body the host sends nothing for is asleep or lost.
constexpr float kMaxSeconds = 1.f;

// Where acFrame's body is aSeconds later (earlier, negative): its velocities on, gravity added while it falls freely.
[[nodiscard]] BodyFrame Predict(const BodyFrame& acFrame, float aSeconds, float aGravity = kGravity) noexcept;

// How far apart two poses are: distance in units and angle in radians.
struct Error
{
    float Distance{};
    float Angle{};
};
[[nodiscard]] Error Between(const BodyFrame& acA, const BodyFrame& acB) noexcept;

// How long a steered body takes to close its gap to its frame's path (Steer).
constexpr float kSteerSeconds = 0.05f;

// The velocities that take a simulated body from where it is onto acTarget's path: the frame's own, plus its gap
// closed over aSeconds. A copy's ragdoll is steered so, not placed: its ragdoll driver keeps it simulated.
struct Velocities
{
    glm::vec3 Linear{};
    glm::vec3 Angular{};
};
[[nodiscard]] Velocities Steer(const glm::vec3& acPosition, const glm::quat& acRotation, const BodyFrame& acTarget,
                               float aSeconds = kSteerSeconds) noexcept;

// A copy's ragdoll body is at its owner's rest: left alone there, so it falls asleep. Steered on (78089/78090 wake
// it each step) it never rests: every dead copy's 18 bodies stayed awake at 3 to 8 units/s, within 0.8 units of their
// rests (kill-move-kills-guest, 14 of 14 runs, 2026-11-11).
constexpr float kRestDistance = 0.5f; // units
constexpr float kRestAngle = 0.03f;   // radians
[[nodiscard]] bool AtRest(const glm::vec3& acPosition, const glm::quat& acRotation, const BodyFrame& acRest) noexcept;

// Whether a body falls freely: its velocity changed by gravity alone between two reads aSeconds apart (a body on
// the ground, sliding or rolling, goes on as it moves instead).
[[nodiscard]] bool FallsFreely(const glm::vec3& acPreviousVelocity, const glm::vec3& acVelocity, float aSeconds,
                               float aGravity = kGravity) noexcept;
} // namespace BodyPrediction
