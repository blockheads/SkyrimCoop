#pragma once

#include <cstdint>
#include <span>
#include <utility>

namespace LockOrderAttribution
{
using Range = std::pair<uintptr_t, uintptr_t>;

inline bool IntroducedByUs(std::span<const uintptr_t> aFrames, Range aModule, std::span<const Range> aPassThrough) noexcept
{
    bool belowEngine = false;
    for (const auto frame : aFrames)
    {
        bool ours = frame >= aModule.first && frame < aModule.second;
        for (const auto& range : aPassThrough)
            if (frame >= range.first && frame < range.second)
                ours = false;
        if (belowEngine && ours)
            return true;
        belowEngine |= !ours;
    }
    return false;
}
}
