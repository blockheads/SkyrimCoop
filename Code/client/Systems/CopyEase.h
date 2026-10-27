#pragma once

#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>

// A copy up from its own ragdoll stands where its engine put it: from there it is eased onto its owner's place over
// kMs, never jumped (a jump shows its skeleton there for a frame: physics-sync.md, "A copy's ragdoll, steered").
struct CopyEase
{
    static constexpr uint64_t kMs = 500;

    glm::vec3 Offset{}; // its own place less its owner's, when it got up
    float Turn{};       // its own heading less its owner's, the short way round
    uint64_t Start{};
    bool Held{}; // placed by its ragdoll (its owner's lying, or its own), not by its owner's movement

    void Begin(const glm::vec3& acOffset, float aTurn, uint64_t aTick) noexcept
    {
        constexpr float cCircle = 6.28318531f;
        Offset = acOffset;
        Turn = std::remainder(aTurn, cCircle);
        Start = aTick ? aTick : 1;
    }

    // Its owner's place at aTick, moved toward its own by what is left of the ease.
    void Apply(uint64_t aTick, glm::vec3& aPosition, float& aHeading) noexcept
    {
        if (!Start)
            return;
        const float cLeft = aTick > Start ? 1.f - static_cast<float>(aTick - Start) / kMs : 1.f;
        if (cLeft <= 0.f)
        {
            Start = 0;
            return;
        }
        constexpr float cCircle = 6.28318531f;
        aPosition += Offset * cLeft;
        aHeading = std::fmod(aHeading + Turn * cLeft + cCircle, cCircle);
    }
};
