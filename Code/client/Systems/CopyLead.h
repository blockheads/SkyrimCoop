#pragma once

#include <algorithm>
#include <cstdint>

// How far ahead of its copy its owner is when a hit on it lands (docs/re/systems/combat-aim.md): the engine predicts
// a target (Actor::GetPredictedPosition 47309) that much further for a copy, so an aim or swing meets its owner.
struct CopyLead
{
    static constexpr uint64_t kMaxMs = 500; // a stalled line: no wild guesses

    // aBehindMs: how far behind its owner the copy shows; aNetworkMs: the line's delay, which the hit takes back.
    [[nodiscard]] static constexpr uint64_t Ms(uint64_t aBehindMs, uint64_t aNetworkMs) noexcept
    {
        return std::min(aBehindMs + aNetworkMs, kMaxMs);
    }
};
