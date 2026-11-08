#pragma once

#include <Structs/AnimationVariables.h>

#include <cstdint>

// Which of its owner's graph variables a copy has, from the snapshots around its playback (CharacterService).
namespace CopyVariables
{
// Integers come from the next snapshot once it is within aAheadMs (one send interval): an owner's graph writes them as a
// clip ends with no action (iSyncTurnState, iSyncIdleLocomotion), up to an interval before the snapshot that carries
// them, and the copy's graph reads them at its own clip's end, with no action either: a machine's bound start, a
// transition's condition (docs/re/systems/animation-replay-recovery.md, "A copy reads its owner's integers ahead").
// Never over what a later action already loaded (aHeldTick), and only a snapshot of the same layout.
inline bool TakeIntegersAhead(AnimationVariables& aShown, const AnimationVariables& acNext, uint64_t aNextTick, uint64_t aHeldTick,
                              uint64_t aPlayback, uint64_t aAheadMs) noexcept
{
    if (aNextTick <= aPlayback || aNextTick - aPlayback > aAheadMs || aNextTick <= aHeldTick ||
        acNext.Integers.size() != aShown.Integers.size() || acNext.Integers == aShown.Integers)
        return false;
    aShown.Integers = acNext.Integers;
    return true;
}
} // namespace CopyVariables
