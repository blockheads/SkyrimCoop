#include <TiltedOnlinePCH.h>

#include <Games/Primitives.h>

void NiRefObject::IncRef()
{
    InterlockedIncrement(&refCount);
}

void NiRefObject::DecRef()
{
    if (Release())
        Destroy();
}

bool NiRefObject::Release()
{
    return InterlockedDecrement(&refCount) == 0;
}

void BSRecursiveLock::Lock()
{
    const auto tid = GetCurrentThreadId();
    _mm_lfence();

    if (m_tid == tid)
    {
        InterlockedIncrement(&m_counter);
    }
    else
    {
        if (InterlockedCompareExchange(&m_counter, 1, 0))
        {
            while (true)
            {
                _mm_pause();
                if (!InterlockedCompareExchange(&m_counter, 1, 0))
                {
                    break;
                }
                std::this_thread::yield();
            }

            _mm_lfence();
        }

        m_tid = tid;
        _mm_sfence();
    }
}

void BSRecursiveLock::Unlock()
{
    _mm_lfence();
    if (m_tid == GetCurrentThreadId())
    {
        if (m_counter == 1)
        {
            m_tid = 0;
            _mm_mfence();
            InterlockedCompareExchange(&m_counter, 0, 1);
        }
        else
        {
            InterlockedDecrement(&m_counter);
        }
    }
}

using TReadWriteLockFunction = void(BSReadWriteLock*);

void BSReadWriteLock::LockForRead()
{
    POINTER_SKYRIMSE(TReadWriteLockFunction, s_lockForRead, 68233);
    s_lockForRead.Get()(this);
}

void BSReadWriteLock::UnlockForRead()
{
    POINTER_SKYRIMSE(TReadWriteLockFunction, s_unlockForRead, 68239);
    s_unlockForRead.Get()(this);
}

void BSReadWriteLock::LockForWrite()
{
    POINTER_SKYRIMSE(TReadWriteLockFunction, s_lockForWrite, 68234);
    s_lockForWrite.Get()(this);
}

void BSReadWriteLock::UnlockForWrite()
{
    POINTER_SKYRIMSE(TReadWriteLockFunction, s_unlockForWrite, 68240);
    s_unlockForWrite.Get()(this);
}
