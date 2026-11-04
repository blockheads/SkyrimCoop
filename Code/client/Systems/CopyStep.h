#pragma once

#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>

// How a copy reaches its owner's place each frame (docs/re/systems/actor-movement.md, "Copies step").
enum class CopyMove : uint8_t
{
    Leave, // its ragdoll places it
    Step,  // its engine steps its character controller there (Actor::Move 37507): it collides and pushes
    Place, // put there (Actor::SetPosition 37309): a warp, pushes nothing
};

struct CopyStep
{
    // Farther off than this it is placed, not stepped (TES3MP snaps past 40 to 80 units).
    static constexpr float kSnap = 64.f;

    // Its owner in the air: placed, as its engine's step keeps it on the ground (gravity and support decide its height).
    [[nodiscard]] static CopyMove Choose(bool aLies, bool aOwnerInAir, const glm::vec3& acAt, const glm::vec3& acGoal) noexcept
    {
        if (aLies)
            return CopyMove::Leave;
        if (aOwnerInAir)
            return CopyMove::Place;
        const glm::vec3 cOff = acGoal - acAt;
        return cOff.x * cOff.x + cOff.y * cOff.y + cOff.z * cOff.z > kSnap * kSnap ? CopyMove::Place : CopyMove::Step;
    }

    // A world offset in an actor's own frame for Actor::Move: x right, y forward (heading aHeading, its rotation z).
    [[nodiscard]] static glm::vec3 Local(const glm::vec3& acOffset, float aHeading) noexcept
    {
        const float cSin = std::sin(aHeading), cCos = std::cos(aHeading);
        return {acOffset.x * cCos - acOffset.y * cSin, acOffset.x * cSin + acOffset.y * cCos, acOffset.z};
    }
};
