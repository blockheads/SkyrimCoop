#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>
#include <vector>

// A copy's answers about its effects (MagicTarget::HasMagicEffect 34517, HasMagicEffectWithKeyword 34518,
// MagicItem::IsSpellTarget 34513): its owner's active effects, as (spell, effect) pairs, sorted. Checked by ClientTests.
namespace OwnerEffects
{
struct Held
{
    uint32_t Spell{};
    uint32_t Effect{};
    auto operator<=>(const Held&) const = default;
};

[[nodiscard]] inline bool Has(const std::vector<Held>& acHeld, uint32_t aEffect) noexcept
{
    return std::ranges::any_of(acHeld, [aEffect](const Held& acEntry) { return acEntry.Effect == aEffect; });
}

[[nodiscard]] inline bool HasSpell(const std::vector<Held>& acHeld, uint32_t aSpell) noexcept
{
    return std::ranges::any_of(acHeld, [aSpell](const Held& acEntry) { return acEntry.Spell == aSpell; });
}

// The first held entry whose effect aHasKeyword(effect id) accepts, or none: its spell is the engine's out value.
template <class THasKeyword> [[nodiscard]] const Held* WithKeyword(const std::vector<Held>& acHeld, THasKeyword&& aHasKeyword) noexcept
{
    const auto it = std::ranges::find_if(acHeld, [&](const Held& acEntry) { return aHasKeyword(acEntry.Effect); });
    return it != acHeld.end() ? &*it : nullptr;
}
} // namespace OwnerEffects
