#pragma once

#include <Structs/BodyFrame.h>

// What every game guesses a body does after a frame (docs/design/physics-sync.md, "Sent when the guess goes
// wrong"): the host sends a body again only where the guests' guess would be wrong. No engine: UnitTests.
namespace BodyPrediction
{
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
// The correction time for a body steered once a frame, given the last frame's seconds: past the frame Havok integrates
// it over, or the gap overshoots (clamped to kSteerSeconds..0.5 s).
[[nodiscard]] float SteerSeconds(float aLastFrameSeconds) noexcept;
// A played reference body's velocities toward its playback's target (docs/design/physics-sync.md, "A watcher's
// object stops where its owner's does"): on the ground it chases the target, in free fall it joins the arc.
[[nodiscard]] Velocities Follow(const glm::vec3& acPosition, const glm::quat& acRotation, const BodyFrame& acTarget, float aLastFrameSeconds) noexcept;

// A body off its rest is steered onto it closing its gap over kRestGlideStep each update (RestPlacer's glide).
constexpr float kRestGlideStep = 0.1f;

// Whether a body is still enough to be put to sleep with its island (60850 sleeps the whole island).
constexpr float kSleepSpeed = 2.f;      // units/s
constexpr float kSleepTurnSpeed = 0.1f; // rad/s
constexpr bool StillToSleep(bool aDynamic, float aSpeed, float aTurnSpeed) noexcept
{
    return aDynamic && aSpeed < kSleepSpeed && aTurnSpeed < kSleepTurnSpeed;
}

// Whether a body falls freely: its velocity changed by gravity alone between two reads aSeconds apart (a body on
// the ground, sliding or rolling, goes on as it moves instead).
[[nodiscard]] bool FallsFreely(const glm::vec3& acPreviousVelocity, const glm::vec3& acVelocity, float aSeconds,
                               float aGravity = kGravity) noexcept;
// Whether a woken body has left where it woke (physics.md, "Woken still"): its pose past the send limits
// (BodySendLimits: 2 units, 0.05 rad). Skyrim wakes islands it never moves, with velocity bursts that go nowhere.
[[nodiscard]] bool LeftRest(const BodyFrame& acNow, const BodyFrame& acWoke) noexcept;

// A body at rest by its own motion (physics-sync.md, "One rule"): within kDistance and kAngle of one pose for kMs,
// whatever Havok's island says (an animated body or a character beside it keeps the island awake).
struct Still
{
    static constexpr float kDistance = 0.5f;
    static constexpr float kAngle = 0.01f; // radians
    static constexpr uint64_t kMs = 300;

    // Each read at aTick (ms): true once it has kept one pose for kMs.
    [[nodiscard]] bool Read(const BodyFrame& acNow, uint64_t aTick) noexcept;

    BodyFrame Anchor{};
    uint64_t Since{};
    bool Started{};
};
} // namespace BodyPrediction
