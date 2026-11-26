#pragma once

#include <cstdint>

namespace ReplayCapture
{
struct Outcome
{
    uint8_t Result;
    bool Capture;
};

template <class Perform> Outcome Run(uint32_t& aFlags, Perform&& aPerform)
{
    // PerformAction (38949) turns a resolved query from flag 1 into flag 2 before returning.
    const bool cQuery = (aFlags & 1) != 0;
    const uint8_t cResult = aPerform();
    return {cResult, !cQuery && (aFlags & 1) == 0};
}
} // namespace ReplayCapture
