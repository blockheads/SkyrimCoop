#pragma once

#include <cstdint>

namespace TestHarness::UpdateTrace
{
struct Counts
{
    uint64_t Updates, Frozen, Saving, Loading, PollOverlaps, PollConcurrent, UpdateOverlaps;
};
[[nodiscard]] Counts Read() noexcept;
} // namespace TestHarness::UpdateTrace
