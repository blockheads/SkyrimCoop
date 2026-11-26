#pragma once

#include <cstdint>

// The engine's recursive spin lock, as CommonLibSSE-NG's BSAtomic.cpp.
struct BSSpinLock
{
    // Spins aPauseAttempts times, then yields (Sleep(0)), then sleeps (Sleep(1)) past kFastSpinThreshold tries.
    void Lock(uint32_t aPauseAttempts = 0) noexcept;

    // Only the owning thread releases it.
    void Unlock() noexcept;

    static constexpr uint32_t kFastSpinThreshold = 10000;

    volatile uint32_t owningThread; // 0
    volatile uint32_t lockCount;    // 4

private:
    volatile long* Count() noexcept { return reinterpret_cast<volatile long*>(&lockCount); }
};
static_assert(sizeof(BSSpinLock) == 0x8);
