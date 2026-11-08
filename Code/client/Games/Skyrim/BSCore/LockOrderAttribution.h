#pragma once

#include <atomic>
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

// Adds a stretch to an edge's history; OR only, so a sighting made while the edge is being published stays.
inline void NoteStretch(std::atomic<uint8_t>& aStretches, uint8_t aStretch) noexcept
{
    if ((aStretches.load(std::memory_order_relaxed) & aStretch) == 0)
        aStretches.fetch_or(aStretch, std::memory_order_relaxed);
}

// An edge's first report of each kind: a risk is reported even after a kept-apart report of the same edge.
inline bool FirstReportOfKind(std::atomic<uint8_t>& aReported, bool aKeptApart) noexcept
{
    const uint8_t cBit = aKeptApart ? 2 : 1;
    return (aReported.fetch_or(cBit) & cBit) == 0;
}

// The frame's claim, checked each run: main loop tasks and our update are never active at once. Once seen together,
// nothing is kept apart for the rest of the run.
struct StretchOverlap
{
    std::atomic<uint32_t> Active[2]{};
    std::atomic<bool> Seen{false};

    // True the first time the two stretches are seen active together.
    bool Enter(uint8_t aStretch) noexcept
    {
        const int cIndex = Index(aStretch);
        if (cIndex < 0)
            return false;
        Active[cIndex].fetch_add(1);
        if (Active[1 - cIndex].load() == 0)
            return false;
        return !Seen.exchange(true);
    }

    void Leave(uint8_t aStretch) noexcept
    {
        if (const int cIndex = Index(aStretch); cIndex >= 0)
            Active[cIndex].fetch_sub(1);
    }

    bool KeptApart(uint8_t aStretch, uint8_t aReverseStretches) const noexcept
    {
        return !Seen.load() && KeptApartByFrame(aStretch, aReverseStretches);
    }

  private:
    static int Index(uint8_t aStretch) noexcept { return aStretch == kMainLoopTasks ? 0 : aStretch == kOurUpdate ? 1 : -1; }
};
}
