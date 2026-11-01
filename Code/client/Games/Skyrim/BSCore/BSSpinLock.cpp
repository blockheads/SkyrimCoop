#include <BSCore/BSSpinLock.h>

#include <intrin.h>

void BSSpinLock::Lock(uint32_t aPauseAttempts) noexcept
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

void BSSpinLock::Unlock() noexcept
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
