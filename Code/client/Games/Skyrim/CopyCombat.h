#pragma once

#include <algorithm>
#include <cstdint>

// The copy contract for combat, without the engine (docs/re/systems/combat-targets.md).
namespace CopyCombat
{
// Actor::StartCombat (38561): a copy never takes a controller or group, CombatUpdateJob (46902) would run its combat AI.
[[nodiscard]] constexpr bool MayStart(bool aCopy) noexcept
{
    return !aCopy;
}

// 41240 for a copy, which has no group: its owner's combat group targets this game's player (form ids). Like 41240
// it asks no combat state.
template <class TTargets>
[[nodiscard]] constexpr bool OnCompass(bool aCopy, const TTargets& acGroupTargets, uint32_t aPlayer) noexcept
{
    return aCopy && std::ranges::find(acGroupTargets, aPlayer) != std::ranges::end(acGroupTargets);
}

// The engine's callers of IsInCombat (38562) / GetCombatState (38556) that get a copy's owner's combat state: none use
// the combat controller after a yes (docs/re/systems/actor-ai-and-copies.md, "Combat state and its readers").
// Address Library ID and the offset after the call.
struct OwnerAnswerSite
{
    uint32_t Id;
    uint32_t ReturnOffset;
};

inline constexpr OwnerAnswerSite cOwnerAnswerSites[] = {
    {21576, 0x38},  // GameFunc::native::IsInCombat
    {21740, 0x28},  // GameFunc::native::GetCombatState
    {38460, 0xC5},  // Actor::GetActorValueRegenRate: the essential health bonus
    {38460, 0x1FC}, // Actor::GetActorValueRegenRate: the combat multiplier
};
} // namespace CopyCombat
