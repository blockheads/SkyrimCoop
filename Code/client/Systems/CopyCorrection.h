#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

// A copy carried past its newest snapshot (InterpolationSystem's short guess) and put right by the late one: the
// difference is blended out over kMs, never jumped (docs/design/copy-clock.md, "A late snapshot"). No engine: ClientTests.
struct CopyCorrection
{
    static constexpr uint64_t kMs = 100;
    static constexpr float kMax = 64.f; // farther off it jumps (CopyStep::kSnap)

    glm::vec3 Offset{};
    uint64_t Start{};

    // Where it showed less where it now goes, at aTick; a teleport-sized one is dropped.
    void Begin(const glm::vec3& acOffset, uint64_t aTick) noexcept
    {
        const bool cNear = glm::length(acOffset) <= kMax;
        Offset = cNear ? acOffset : glm::vec3{};
        Start = cNear ? (aTick ? aTick : 1) : 0;
    }

    // What is left of it at aTick.
    [[nodiscard]] glm::vec3 At(uint64_t aTick) const noexcept
    {
        if (!Start || aTick >= Start + kMs)
            return {};
        const float cLeft = aTick > Start ? 1.f - static_cast<float>(aTick - Start) / kMs : 1.f;
        return Offset * cLeft;
    }
};
