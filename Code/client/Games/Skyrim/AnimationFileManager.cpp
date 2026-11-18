#include <Games/Skyrim/AnimationFileManager.h>

AnimationFileManager* AnimationFileManager::Get() noexcept
{
    POINTER_SKYRIMSE(AnimationFileManager*, s_singleton, 401579);
    return *s_singleton.Get();
}

void AnimationFileManager::Cancel(void* apLoad) noexcept
{
    using TCancel = void(AnimationFileManager*, void*);
    POINTER_SKYRIMSE(TCancel, s_cancel, 63971);
    if (apLoad)
        s_cancel(this, apLoad);
}
