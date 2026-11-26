#pragma once
#include <cstdint>

namespace HitVisualsRules
{
constexpr uint32_t kPersist = 1u << 12;
constexpr uint32_t kNoHit = 1u << 27;
inline bool Flash(uint32_t aFlags, bool aHasVisuals, float aDuration, uint32_t aCastingType) noexcept
{
    return aHasVisuals && !(aFlags & kNoHit) && (!(aFlags & kPersist) || (aDuration <= 0.f && aCastingType == 1));
}
inline bool Allowed(uint32_t aFlags, bool aPersisting) noexcept
{
    return !(aFlags & kNoHit) && (!aPersisting || (aFlags & kPersist));
}
} // namespace HitVisualsRules
