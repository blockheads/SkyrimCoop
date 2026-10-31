#pragma once

#include <cstdint>

namespace ActorPools
{
// The pools: health, magicka and stamina (ActorValueInfo kHealth..kStamina).
constexpr int32_t kHealth = 24;
constexpr int32_t kMagicka = 25;
constexpr int32_t kStamina = 26;
constexpr int32_t kAll[]{kHealth, kMagicka, kStamina};

constexpr bool IsPool(int64_t aIndex) noexcept
{
    return aIndex >= kHealth && aIndex <= kStamina;
}

// ActorValueOwner::ForceMode.
enum Mode : int32_t
{
    kPermanent = 0,
    kTemporary = 1,
    kDamage = 2,
};

// A copy's pools made its owner's: the maximum, the temporary modifier (an enchantment's fortify) as a delta, then
// the current value.
template <class Values, class ReadCurrent, class ReadTemporary, class Force>
void Restore(const Values& acValues, ReadCurrent&& aCurrent, ReadTemporary&& aTemporary, Force&& aForce) noexcept
{
    for (const int32_t cIndex : kAll)
    {
        if (const auto max = acValues.ActorMaxValuesList.find(cIndex); max != acValues.ActorMaxValuesList.end())
            aForce(kPermanent, cIndex, max->second);
        if (const auto temp = acValues.ActorTemporaryValuesList.find(cIndex); temp != acValues.ActorTemporaryValuesList.end())
        {
            const float cDelta = temp->second - aTemporary(cIndex);
            if (cDelta != 0.f)
                aForce(kTemporary, cIndex, aCurrent(cIndex) + cDelta);
        }
        if (const auto value = acValues.ActorValuesList.find(cIndex); value != acValues.ActorValuesList.end())
            aForce(kDamage, cIndex, value->second);
    }
}
} // namespace ActorPools
