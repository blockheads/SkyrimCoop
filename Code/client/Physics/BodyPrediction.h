#pragma once

#include <Structs/BodyFrame.h>

// What every game guesses a body does after a frame (docs/design/physics-sync.md, "Sent when the guess goes
// wrong"): the host sends a body again only where the guests' guess would be wrong. No engine: ClientTests.
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

// Whether a body falls freely: its velocity changed by gravity alone between two reads aSeconds apart (a body on
// the ground, sliding or rolling, goes on as it moves instead).
[[nodiscard]] bool FallsFreely(const glm::vec3& acPreviousVelocity, const glm::vec3& acVelocity, float aSeconds,
                               float aGravity = kGravity) noexcept;
} // namespace BodyPrediction
