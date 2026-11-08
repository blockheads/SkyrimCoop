#pragma once

#include <Structs/AnimationVariables.h>

#include <algorithm>
#include <cstdint>
#include <limits>

// Which of its owner's graph variables a copy has, from the snapshots around its playback (CharacterService).
// docs/re/systems/animation-replay-recovery.md, "A copy reads its owner's integers ahead".
namespace CopyVariables
{
constexpr uint64_t kNoAction = std::numeric_limits<uint64_t>::max();

// The owner time a copy's graph is at: its last replayed action's tick plus the animation time since. Its clips run
// at the engine's frame rate while its playback steers at 0.85-1.1x (CopyClock.h); kept within [playback,
// playback + aMostAhead].
struct GraphClock
{
    double Tick = 0.0;

    void ActionTaken(uint64_t aActionTick) noexcept { Tick = static_cast<double>(aActionTick); }
    uint64_t Advance(uint64_t aPlayback, double aFrameMs, uint64_t aMostAhead) noexcept
    {
        Tick = std::clamp(Tick + aFrameMs, static_cast<double>(aPlayback), static_cast<double>(aPlayback + aMostAhead));
        return static_cast<uint64_t>(Tick);
    }
};

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

// Integers: the newest snapshot within one send interval (aAheadMs) past the copy's graph time. An owner's graph
// writes them as a clip ends with no action (iSyncTurnState, iSyncIdleLocomotion), up to an interval before the
// snapshot that carries them, and the copy's graph reads them at its own clip's end. Never older than what is held
// (aHeldTick), never at or past an action not yet played (aNextAction: it loads the owner's integers of its own time,
// staged until its event), and only a snapshot with as many integers.
template <class Points>
[[nodiscard]] const AnimationVariables* IntegersAhead(const Points& acPoints, const AnimationVariables& acShown, uint64_t aHeldTick,
                                                      uint64_t aGraphTick, uint64_t aAheadMs, uint64_t aNextAction) noexcept
{
    const AnimationVariables* pTaken = nullptr;
    for (const auto& cPoint : acPoints)
    {
        if (cPoint.Tick <= aHeldTick || cPoint.Tick >= aNextAction || cPoint.Tick > aGraphTick + aAheadMs)
            continue;
        if (cPoint.Variables.Integers.size() == acShown.Integers.size())
            pTaken = &cPoint.Variables;
    }
    return pTaken && pTaken->Integers != acShown.Integers ? pTaken : nullptr;
}
} // namespace CopyVariables
