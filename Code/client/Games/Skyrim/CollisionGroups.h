#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// A set of collision groups, one bit each: published by the main thread, read by the physics threads without a lock.
struct CollisionGroups
{
    [[nodiscard]] bool Has(uint32_t aGroup) const noexcept
    {
        return Bits[(aGroup >> 6) & 1023].load(std::memory_order_relaxed) & (1ull << (aGroup & 63));
    }

    // acGroups sorted, unique. New groups are marked before old ones clear: one in both never reads as unmarked.
    bool Publish(const std::vector<uint16_t>& acGroups) noexcept
    {
        if (acGroups == Marked)
            return false;
        for (const uint16_t cGroup : acGroups)
            Bits[cGroup >> 6].fetch_or(1ull << (cGroup & 63), std::memory_order_relaxed);
        for (const uint16_t cGroup : Marked)
            if (!std::ranges::binary_search(acGroups, cGroup))
                Bits[cGroup >> 6].fetch_and(~(1ull << (cGroup & 63)), std::memory_order_relaxed);
        Marked = acGroups;
        return true;
    }

    std::array<std::atomic<uint64_t>, 0x10000 / 64> Bits{};
    std::vector<uint16_t> Marked; // main thread: the groups set in Bits
};
