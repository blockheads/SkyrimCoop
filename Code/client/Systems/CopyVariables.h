#pragma once

#include <Structs/AnimationVariables.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

// Which of its owner's graph variables a copy has, from the snapshots around its playback (CharacterService).
// docs/re/systems/animation-replay-recovery.md, "A copy reads its owner's integers ahead".
namespace CopyVariables
{
constexpr uint64_t kNoAction = std::numeric_limits<uint64_t>::max();

// The first action not yet played (kNoAction for none): a staged one stays queued until its event is sent.
template <class Actions> [[nodiscard]] uint64_t NextAction(const Actions& acActions) noexcept
{
    uint64_t next = kNoAction;
    for (const auto& cAction : acActions)
        next = std::min(next, cAction.Tick);
    return next;
}

// Whether the held snapshot replaces the copy's variables: newer than what it has, and before any action not yet
// played. A staged action's variables are in its graph until its event (ReplayStep.h): a snapshot after it waits.
[[nodiscard]] inline bool TakesHeld(uint64_t aHeldTick, uint64_t aVariablesTick, uint64_t aNextAction) noexcept
{
    return aHeldTick > aVariablesTick && aHeldTick < aNextAction;
}

// The newest snapshot at or before playback (the first when none is): what it holds stays shown once playback reaches
// it, also while it extrapolates past the last one.
template <class Points> [[nodiscard]] const auto& Held(const Points& acPoints, uint64_t aPlayback) noexcept
{
    const auto* pHeld = &acPoints.front();
    for (const auto& cPoint : acPoints)
        if (cPoint.Tick <= aPlayback)
            pHeld = &cPoint;
    return *pHeld;
}

// A copy's graph time: the owner tick its graph is at. Set where its graph takes its owner's moment (an action's event
// sent, a saved graph loaded), stepped by each graph update's delta (20123, after the update; 41453 passes the frame's
// seconds). Written in its animation job and on the main thread.
struct GraphTime
{
    std::atomic<double> Tick{-1.0}; // below 0: not set yet

    GraphTime() = default;
    GraphTime(const GraphTime& acOther) noexcept : Tick(acOther.Tick.load(std::memory_order_relaxed)) {}
    GraphTime& operator=(const GraphTime& acOther) noexcept
    {
        Tick.store(acOther.Tick.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }
    void Anchor(uint64_t aTick) noexcept { Tick.store(static_cast<double>(aTick), std::memory_order_relaxed); }
    void Step(double aMs) noexcept
    {
        if (Tick.load(std::memory_order_relaxed) >= 0.0)
            Tick.fetch_add(aMs, std::memory_order_relaxed);
    }
};

// What a copy's integers are read against: its graph's time, or playback when it has none or is further from playback
// than aMostApart (its graph stood still or never took its owner's moment: it follows no clip of its owner's).
[[nodiscard]] inline uint64_t Reference(double aGraphTick, uint64_t aPlayback, uint64_t aMostApart) noexcept
{
    if (aGraphTick < 0.0 || std::abs(aGraphTick - static_cast<double>(aPlayback)) > static_cast<double>(aMostApart))
        return aPlayback;
    return static_cast<uint64_t>(aGraphTick);
}

// The owner's integers by owner tick, from its snapshots and from each action as it is staged (in tick order, an action
// after a snapshot of its tick).
struct IntegersAt
{
    uint64_t Tick{};
    std::vector<uint32_t> Integers;
    bool Action = false;
};
using IntegerTimeline = std::deque<IntegersAt>;

inline void Record(IntegerTimeline& aTimeline, uint64_t aTick, const std::vector<uint32_t>& acIntegers, bool aAction)
{
    const auto cBefore = [](const IntegersAt& acEntry, std::pair<uint64_t, bool> aKey) {
        return std::pair{acEntry.Tick, acEntry.Action} < aKey;
    };
    const auto it = std::lower_bound(aTimeline.begin(), aTimeline.end(), std::pair{aTick, aAction}, cBefore);
    if (it != aTimeline.end() && it->Tick == aTick && it->Action == aAction)
        return;
    aTimeline.insert(it, IntegersAt{aTick, acIntegers, aAction});
}

// Keeps the newest entry at or before aOldest and everything after it.
inline void Forget(IntegerTimeline& aTimeline, uint64_t aOldest) noexcept
{
    while (aTimeline.size() > 1 && aTimeline[1].Tick <= aOldest)
        aTimeline.pop_front();
}

// The integers a copy's graph reads at aGraph: the newest recorded up to an interval past it (an owner's graph writes
// them as a clip ends with no action, iSyncTurnState and iSyncIdleLocomotion, up to an interval before the snapshot that
// carries them). Before an action not yet played (aNextAction), only that action's own: staged, its integers are in the
// graph until its event (ReplayStep.h). Only entries with aCount integers.
[[nodiscard]] inline const std::vector<uint32_t>* At(const IntegerTimeline& acTimeline, uint64_t aGraph, uint64_t aAheadMs,
                                                     uint64_t aNextAction, size_t aCount) noexcept
{
    const std::vector<uint32_t>* pAt = nullptr;
    for (const auto& cEntry : acTimeline)
    {
        if (cEntry.Tick > aGraph + aAheadMs || cEntry.Tick > aNextAction)
            break;
        if (cEntry.Integers.size() == aCount && (cEntry.Action || cEntry.Tick < aNextAction))
            pAt = &cEntry.Integers;
    }
    return pAt;
}
} // namespace CopyVariables
