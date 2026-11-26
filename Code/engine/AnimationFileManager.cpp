#include <AnimationFileManager.h>

AnimationFileManager* AnimationFileManager::Get() noexcept
{
    POINTER_SKYRIMSE(AnimationFileManager*, s_singleton, 401579);
    return *s_singleton.Get();
}
