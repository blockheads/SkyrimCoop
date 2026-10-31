#pragma once

#include <bit>
#include <cstdint>

namespace LockOrderHistory
{
// A mode belongs to its own observation: a later write must not borrow a reader's callers or thread.
constexpr uint64_t Key(uint64_t aOuter, uint64_t aInner, uint8_t aOuterMode, uint8_t aInnerMode) noexcept
{
    uint64_t hash = aOuter * 0x9E3779B97F4A7C15ull;
    hash ^= std::rotl(aInner, 31) * 0xC2B2AE3D27D4EB4Full;
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
