#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace LockOrderAttribution
{
using Range = std::pair<uintptr_t, uintptr_t>;
// LockOrder::IgnoreFrames' slots; test_coop_harness checks the calls fit.
inline constexpr size_t kMaxPassThrough = 32;

inline bool IntroducedByUs(std::span<const uintptr_t> aFrames, Range aModule, std::span<const Range> aPassThrough) noexcept
{
    bool belowEngine = false;
    for (const auto frame : aFrames)
    {
        bool ours = frame >= aModule.first && frame < aModule.second;
        for (const auto& range : aPassThrough)
            if (frame >= range.first && frame < range.second)
                ours = false;
        if (belowEngine && ours)
            return true;
        belowEngine |= !ours;
    }
    return false;
}

// The frame's stretches a lock order is taken in, as bits (lock-order.md, Orders the frame keeps apart).
enum Stretch : uint8_t
{
    kElsewhere = 1,
    kMainLoopTasks = 2, // HookMainLoop's drain of MainLoopTasks, main thread
    kOurUpdate = 4,     // HookVMUpdate's World::Update, in the VM update job
};

// An inversion whose reverse order was only ever seen in a stretch that never runs alongside this one. Elsewhere on
// either side, or the reverse also seen elsewhere, is a risk.
inline bool KeptApartByFrame(uint8_t aStretch, uint8_t aReverseStretches) noexcept
{
    return (aStretch == kMainLoopTasks && aReverseStretches == kOurUpdate) ||
           (aStretch == kOurUpdate && aReverseStretches == kMainLoopTasks);
}
}
