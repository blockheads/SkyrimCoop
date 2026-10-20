#pragma once

#include <intrin.h>

// The engine's recursive spin lock, inlined wherever the engine takes it; as CommonLibSSE-NG's (BSAtomic.cpp).
struct BSSpinLock
{
    // Spins aPauseAttempts times, then yields (Sleep(0)), then sleeps (Sleep(1)) past kFastSpinThreshold tries.
    void Lock(uint32_t aPauseAttempts = 0) noexcept
    {
        const auto cThread = static_cast<uint32_t>(GetCurrentThreadId());
        _mm_lfence();
        if (owningThread == cThread)
        {
            _InterlockedIncrement(Count());
            return;
        }
        if (_InterlockedCompareExchange(Count(), 1, 0) != 0)
        {
            uint32_t attempts = 0;
            do
            {
                _mm_pause();
                if (++attempts >= aPauseAttempts)
                {
                    uint32_t spins = 0;
                    while (_InterlockedCompareExchange(Count(), 1, 0) != 0)
                        Sleep(++spins < kFastSpinThreshold ? 0 : 1);
                    break;
                }
            } while (_InterlockedCompareExchange(Count(), 1, 0) != 0);
            _mm_lfence();
        }
        owningThread = cThread;
        _mm_sfence();
    }

    // Only the owning thread releases it.
    void Unlock() noexcept
    {
        _mm_lfence();
        if (owningThread != static_cast<uint32_t>(GetCurrentThreadId()))
            return;
        if (lockCount == 1)
        {
            owningThread = 0;
            _mm_mfence();
            _InterlockedCompareExchange(Count(), 0, 1);
        }
        else
            _InterlockedDecrement(Count());
    }

    static constexpr uint32_t kFastSpinThreshold = 10000;

    volatile uint32_t owningThread; // 0
    volatile uint32_t lockCount;    // 4

private:
    volatile long* Count() noexcept { return reinterpret_cast<volatile long*>(&lockCount); }
};
static_assert(sizeof(BSSpinLock) == 0x8);
