#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// A copy's answer to "has this effect" (MagicTarget::HasMagicEffect 34517, HasMagicEffectWithKeyword 34518): its
// owner's active effects, sorted ids. Checked by ClientTests.
namespace OwnerEffects
{
[[nodiscard]] inline bool Has(const std::vector<uint32_t>& acHeld, uint32_t aEffect) noexcept
{
    return std::ranges::binary_search(acHeld, aEffect);
}

// aHasKeyword(effect id): that effect's form carries the keyword asked about.
template <class THasKeyword> [[nodiscard]] bool HasWithKeyword(const std::vector<uint32_t>& acHeld, THasKeyword&& aHasKeyword) noexcept
{
    return std::ranges::any_of(acHeld, aHasKeyword);
}
} // namespace OwnerEffects
