#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

struct Actor;

struct hkpRigidBody;

// What this game's player's character pushes and its swings hit (Havok's character proxy giving a dynamic body an impulse,
// bhkCharProxyController::objectInteractionCallback 79133), for a guest to push the host's bodies alike
// (docs/design/physics-sync.md, "A guest's own actions").
namespace PlayerHands
{
struct Push
{
    uint32_t Object{};           // the body's reference (form id)
    const hkpRigidBody* pBody{}; // to find its index; compared, never read
    glm::vec3 Impulse{};         // the game's units (mass x units/s)
    glm::vec3 Point{};
};

// Main thread, each update: the pushes since the last call, in order (none while not watching).
[[nodiscard]] std::vector<Push> Take() noexcept;
// Main thread, each update: the impulses its weapon swings gave bodies since the last call
// (Actor::CalculateCurrentHitTargetForWeaponSwing 38628's applyPointImpulse), in order.
[[nodiscard]] std::vector<Push> TakeHits() noexcept;
// Off: nothing is recorded.
void Watch(bool aOn) noexcept;
// Main thread, each update: the copies in this game. A copy's capsule pushes no object (its impulse is zeroed in
// 79133): what its owner's character walks into is moved by its owner's game.
void SetCopies(const std::vector<uint32_t>& acCopyFormIds) noexcept;
// Whether a copy's swing runs 38628 on this thread now: what it hits is its owner's game's (a destructible's damage).
[[nodiscard]] bool CopySwings() noexcept;
// The actor whose swing 38628 runs on this thread now, else null.
[[nodiscard]] Actor* Swinger() noexcept;
} // namespace PlayerHands
