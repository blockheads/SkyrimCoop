#pragma once

#include <algorithm>
#include <cstdint>

// How far ahead of its copy its owner is when a hit on it lands (docs/re/systems/combat-aim.md): the engine predicts
// a target (Actor::GetPredictedPosition 47309) that much further for a copy, so an aim or swing meets its owner.
struct CopyLead
{
    static constexpr uint64_t kMaxMs = 500; // a stalled line: no wild guesses

    // aBehindMs: how far behind its owner the copy shows; aLineMs: the delay to its owner's game (CopyLine).
    [[nodiscard]] static constexpr uint64_t Ms(uint64_t aBehindMs, uint64_t aLineMs) noexcept
    {
        return std::min(aBehindMs + aLineMs, kMaxMs);
    }
};

// One copy's line to its owner's game: its snapshots' lateness, smoothed. Its own owner's, not the game-wide worst
// the copies' clock plays behind (CopyClock::Network), which another game's slow line raises.
class CopyLine
{
public:
    static constexpr double kWeight = 0.1; // of each new snapshot's lateness

    // A snapshot of it stamped aOwnerTick (the session's time) arrived at aArrived.
    void OnSnapshot(uint64_t aOwnerTick, uint64_t aArrived) noexcept
    {
        const double cLate = aArrived > aOwnerTick ? static_cast<double>(aArrived - aOwnerTick) : 0.0;
        m_ms = m_started ? m_ms + kWeight * (cLate - m_ms) : cLate;
        m_started = true;
    }
    [[nodiscard]] uint64_t Ms() const noexcept { return static_cast<uint64_t>(m_ms + 0.5); }

private:
    double m_ms{};
    bool m_started{};
};
