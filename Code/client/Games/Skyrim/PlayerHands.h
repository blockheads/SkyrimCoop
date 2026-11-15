#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

struct hkpRigidBody;

// What this game's player's character pushes (Havok's character proxy giving a dynamic body an impulse,
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
// Off: nothing is recorded.
void Watch(bool aOn) noexcept;
} // namespace PlayerHands
