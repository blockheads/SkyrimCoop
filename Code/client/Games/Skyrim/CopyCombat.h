#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// The copy contract for combat, without the engine (docs/re/systems/combat-targets.md, "A copy's combat"): a copy holds
// a real controller and group filled from its owner's fight; nothing on its own game decides one.
namespace CopyCombat
{
// Actor::StartCombat (38561) and the group primitives (46873 / 46874): a copy only through Hold, putting its owner's
// fight on it.
[[nodiscard]] constexpr bool MayStart(bool aCopy, bool aHolding) noexcept
{
    return !aCopy || aHolding;
}

// What a copy's controller needs to match its owner's fight.
enum class Step : uint8_t
{
    kNone,
    kStart, // a new group and controller (46873), the owner's group not here yet
    kJoin,  // the group another copy of the owner's group holds (46874)
    kStop,  // its owner's fight ended (StopCombat 38566)
};

// A player's copy too: its controller holds its player's own group (the player has no controller of its own).
[[nodiscard]] constexpr Step Next(uint8_t aOwnerState, bool aHasController, bool aOwnerGroupHere, bool aHasTarget) noexcept
{
    if (aOwnerState == 0)
        return aHasController ? Step::kStop : Step::kNone;
    if (aHasController)
        return Step::kNone;
    if (aOwnerGroupHere)
        return Step::kJoin;
    return aHasTarget ? Step::kStart : Step::kNone;
}

// The owner's group's targets the copy's group lacks, and those it holds that the owner's group dropped.
struct TargetChanges
{
    std::vector<uint32_t> Add;
    std::vector<uint32_t> Remove;
};

[[nodiscard]] inline TargetChanges Diff(const std::vector<uint32_t>& acHeld, const std::vector<uint32_t>& acOwner)
{
    TargetChanges changes;
    for (const uint32_t cTarget : acOwner)
        if (cTarget && std::ranges::find(acHeld, cTarget) == acHeld.end())
            changes.Add.push_back(cTarget);
    for (const uint32_t cTarget : acHeld)
        if (std::ranges::find(acOwner, cTarget) == acOwner.end())
            changes.Remove.push_back(cTarget);
    return changes;
}

// GetCombatState (38556) reads 2 while the group's searchState (+0xD8) is set.
[[nodiscard]] constexpr uint32_t SearchState(uint8_t aOwnerState) noexcept
{
    return aOwnerState == 2 ? 1 : 0;
}
} // namespace CopyCombat
