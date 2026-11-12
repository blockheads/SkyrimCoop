#include <Physics/BodyPrediction.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace BodyPrediction
{
BodyFrame Predict(const BodyFrame& acFrame, float aSeconds, float aGravity) noexcept
{
    const float cSeconds = std::clamp(aSeconds, -kMaxSeconds, kMaxSeconds);
    BodyFrame frame = acFrame;
    const glm::vec3 cGravity = acFrame.Gravity ? glm::vec3(0.f, 0.f, aGravity) : glm::vec3(0.f);
    frame.Position = acFrame.Position + acFrame.LinearVelocity * cSeconds + 0.5f * cGravity * cSeconds * cSeconds;
    frame.LinearVelocity = acFrame.LinearVelocity + cGravity * cSeconds;
    // A world-space angular velocity turns the rotation about its axis.
    const float cSpeed = glm::length(acFrame.AngularVelocity);
    if (cSpeed > 1e-6f)
        frame.Rotation = glm::normalize(glm::angleAxis(cSpeed * cSeconds, acFrame.AngularVelocity / cSpeed) * acFrame.Rotation);
    return frame;
}

Error Between(const BodyFrame& acA, const BodyFrame& acB) noexcept
{
    const float cDot = std::min(1.f, std::abs(glm::dot(acA.Rotation, acB.Rotation)));
    return {glm::distance(acA.Position, acB.Position), 2.f * std::acos(cDot)};
}

Velocities Steer(const glm::vec3& acPosition, const glm::quat& acRotation, const BodyFrame& acTarget, float aSeconds) noexcept
{
    Velocities velocities{acTarget.LinearVelocity + (acTarget.Position - acPosition) / aSeconds, acTarget.AngularVelocity};
    // The world-space turn from where it is to the frame's, the short way round.
    glm::quat turn = glm::normalize(acTarget.Rotation * glm::inverse(acRotation));
    if (turn.w < 0.f)
        turn = -turn;
    const float cSin = glm::length(glm::vec3(turn.x, turn.y, turn.z));
    if (cSin > 1e-6f)
        velocities.Angular += glm::vec3(turn.x, turn.y, turn.z) / cSin * (2.f * std::atan2(cSin, turn.w) / aSeconds);
    return velocities;
}

bool FallsFreely(const glm::vec3& acPreviousVelocity, const glm::vec3& acVelocity, float aSeconds, float aGravity) noexcept
{
    if (aSeconds <= 0.f)
        return false;
    const glm::vec3 cAcceleration = (acVelocity - acPreviousVelocity) / aSeconds;
    // Within a quarter of gravity of falling, on every axis (air drag and damping are small).
    const float cTolerance = std::abs(aGravity) * 0.25f;
    return std::abs(cAcceleration.x) < cTolerance && std::abs(cAcceleration.y) < cTolerance &&
           std::abs(cAcceleration.z - aGravity) < cTolerance;
}
} // namespace BodyPrediction
