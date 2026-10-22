#include <Games/Misc/Lock.h>

void Lock::SetLock(bool aIsLocked) noexcept
{
    using TSetLock = void(Lock*, bool);
    POINTER_SKYRIMSE(TSetLock, realSetLock, 12401);

    return realSetLock(this, aIsLocked);
}
