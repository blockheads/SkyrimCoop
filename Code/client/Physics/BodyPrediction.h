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

// A copy's ragdoll at its owner's rest (docs/re/systems/physics.md, A copy at its owner's rest). Steered there on every
// step with no end (78089/78090 wake a body each time) it never slept: every dead copy's 18 bodies stayed awake at 3
// to 8 units/s within 0.8 units of their rests (kill-move-kills-guest, 14 of 14 runs, 2026-11-11).
// Its bodies are steered toward the rest for this many physics steps, then all put exactly there in one step and
// left. About three of them never come within half a unit while the others are still on their way (their joints hold
// them off: 190 steers a second after the rest, for good); the owner's poses fit its joints together.
constexpr uint16_t kRestSettleSteps = 30;
enum class RestStep
{
    kSteer, // on its way
    kPlace, // every body's rest arrived in the same step: all are put there in this one
    kLeave, // there: left to fall asleep
};
constexpr RestStep StepToRest(uint16_t aSteps) noexcept
{
    return aSteps < kRestSettleSteps ? RestStep::kSteer : aSteps == kRestSettleSteps ? RestStep::kPlace : RestStep::kLeave;
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
