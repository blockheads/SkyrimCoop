#pragma once

#include <cstdint>

// A reference's name (ExtraTextDisplayData, docs/re/systems/names.md): a quest alias's message name regenerates from
// its message once its cache is cleared (12769); a custom name (no message, no quest, instance -2) is only that cache.
namespace DisplayNames
{
inline constexpr int32_t kCustomInstance = -2;

// TESQuest::ClearAliasNameCaches (after a stores-text alias's fill) may clear this name's cache: not a custom name.
[[nodiscard]] constexpr bool CacheClears(bool aHasMessage, bool aHasQuest, int32_t aInstance) noexcept
{
    return aHasMessage || aHasQuest || aInstance != kCustomInstance;
}
} // namespace DisplayNames
