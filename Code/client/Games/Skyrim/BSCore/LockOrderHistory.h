#pragma once

#include <array>
#include <atomic>

#include <bit>
#include <cstdint>

namespace LockOrderHistory
{
// Each destroyed lock's generation, by its address (high 48 bits) and count (low 16): another lock's destruction never
// moves a lock's own (lock-order.md, "A lock freed and used again").
template <size_t N> struct LockGenerations
{
    static_assert((N & (N - 1)) == 0);
    std::array<std::atomic<uint64_t>, N> Slots{};

    static size_t Home(uint64_t aLock) noexcept { return static_cast<size_t>(((aLock >> 3) ^ (aLock >> 19)) & (N - 1)); }

    uint32_t Of(uint64_t aLock) const noexcept
    {
        for (size_t probe = 0; probe < 8; ++probe)
        {
            const uint64_t cSlot = Slots[(Home(aLock) + probe) & (N - 1)].load(std::memory_order_relaxed);
            if (cSlot == 0)
                return 0;
            if ((cSlot >> 16) == aLock)
                return static_cast<uint32_t>(cSlot & 0xFFFF);
        }
        return 0;
    }

    void Destroyed(uint64_t aLock) noexcept
    {
        for (size_t probe = 0; probe < 8; ++probe)
        {
            auto& slot = Slots[(Home(aLock) + probe) & (N - 1)];
            uint64_t seen = slot.load(std::memory_order_relaxed);
            while (seen == 0 || (seen >> 16) == aLock)
            {
                const uint64_t cNext = (aLock << 16) | ((seen == 0 ? 0 : (seen + 1) & 0xFFFF) | (seen == 0 ? 1 : 0));
                if (slot.compare_exchange_weak(seen, cNext, std::memory_order_relaxed))
                    return;
            }
        }
        Slots[Home(aLock)].store((aLock << 16) | 1, std::memory_order_relaxed);
    }
};

// A mode belongs to its own observation: a later write must not borrow a reader's callers or thread. A lock's generation
// counts the objects destroyed at its address: a pair seen on an earlier object is not this one's.
constexpr uint64_t Key(uint64_t aOuter, uint64_t aInner, uint8_t aOuterMode, uint8_t aInnerMode, uint32_t aOuterGeneration = 0,
                       uint32_t aInnerGeneration = 0) noexcept
{
    uint64_t hash = aOuter * 0x9E3779B97F4A7C15ull;
    hash ^= std::rotl(aInner, 31) * 0xC2B2AE3D27D4EB4Full;
    hash ^= ((uint64_t{aOuterGeneration} << 32) | aInnerGeneration) * 0xD6E8FEB86659FD93ull;
    hash ^= hash >> 29;
    return (hash & ~uint64_t{7}) | (uint64_t{aOuterMode} << 2) | (uint64_t{aInnerMode} << 1) | 1;
}

// An inverted ask as it happens: is another thread holding the asked lock, and is it inside the reverse order (took it
// at the reverse pair's outer call site)? kLive: the two orders overlap now, the deadlock's precondition met.
enum class Overlap : uint8_t
{
    kNone,
    kContended,
    kLive,
};

// aOtherHeld: one other thread's held entries ({Lock, Caller, Acquired}).
template <class Entries>
constexpr Overlap OverlapOf(const Entries& acOtherHeld, const void* apAsked, uint64_t aReverseOuterCaller) noexcept
{
    Overlap overlap = Overlap::kNone;
    for (const auto& cHeld : acOtherHeld)
        if (cHeld.Lock == apAsked && cHeld.Acquired)
        {
            if (cHeld.Caller == aReverseOuterCaller)
                return Overlap::kLive;
            overlap = Overlap::kContended;
        }
    return overlap;
}
} // namespace LockOrderHistory
