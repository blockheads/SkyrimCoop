#pragma once

#include <cstdint>

namespace KnockReplay
{
[[nodiscard]] inline uint8_t AfterAction(uint8_t aCopy, bool aGetUp, bool aTaken) noexcept
{
    return aTaken && aGetUp && aCopy >= 1 && aCopy <= 4 ? 5 : aCopy;
}
// A copy on its owner's stream: its owner's get-up action stands its own body up (BeginGetUp 40150, knock state 5);
// its graph takes the get-up from the stream.
[[nodiscard]] inline bool StandsUp(uint8_t aCopy, bool aGetUp) noexcept
{
    return AfterAction(aCopy, aGetUp, true) != aCopy;
}
} // namespace KnockReplay
