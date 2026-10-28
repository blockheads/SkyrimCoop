#pragma once

#include <cstdint>

namespace KnockReplay
{
[[nodiscard]] inline uint8_t AfterAction(uint8_t aCopy, bool aGetUp, bool aTaken) noexcept
{
    return aTaken && aGetUp && aCopy >= 1 && aCopy <= 4 ? 5 : aCopy;
}
} // namespace KnockReplay
