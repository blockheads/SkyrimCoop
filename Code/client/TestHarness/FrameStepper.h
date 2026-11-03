#pragma once

#include <cstdint>

namespace TestHarness
{
// The world frozen for a repro, stepped a frame at a time (docs/design/repro-mcp.md). Pure: ClientTests.
// Polled once a frame from our update, which runs after the frame's world update (Main::Update 36564).
class FrameStepper
{
public:
    void Freeze(bool aFrozen) noexcept
    {
        m_frozen = aFrozen;
        m_steps = 0;
    }
    // Lets aFrames more world updates run, then freezes again.
    void Step(uint32_t aFrames) noexcept { m_steps += aFrames; }

    // aRan: this frame's world update ran (not frozen, no pausing menu). Returns whether the next frame is frozen.
    [[nodiscard]] bool Poll(bool aRan) noexcept
    {
        if (aRan)
        {
            ++m_worldFrames;
            if (m_steps)
                --m_steps;
        }
        return m_frozen && !m_steps;
    }

    [[nodiscard]] bool Frozen() const noexcept { return m_frozen; }
    [[nodiscard]] uint32_t StepsLeft() const noexcept { return m_steps; }
    [[nodiscard]] uint64_t WorldFrames() const noexcept { return m_worldFrames; }

private:
    bool m_frozen = false;
    uint32_t m_steps = 0;
    uint64_t m_worldFrames = 0;
};
} // namespace TestHarness
