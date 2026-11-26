#pragma once

#include <cstdint>

struct Actor;
struct CombatGroup;
struct CopyCombatFields;

// A copy's real combat controller and group, held to its owner's fight (combat-targets.md, "A copy's combat").
namespace CopyCombatGroups
{
void Hold(Actor& aCopy, const CopyCombatFields& acFields) noexcept;
// Inside Hold: the group primitives and CanAttackActor let the copy in (the owner's fight already decided it).
[[nodiscard]] bool Holding() noexcept;
// Every member a copy: its group's own update (44703) is not run, its targets stay its owners'.
[[nodiscard]] bool HeldByOwners(const CombatGroup& acGroup) noexcept;
} // namespace CopyCombatGroups
