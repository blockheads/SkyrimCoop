#include <TiltedOnlinePCH.h>

#include <Interface/CrosshairPickData.h>

CrosshairPickData* CrosshairPickData::Get() noexcept
{
    POINTER_SKYRIMSE(CrosshairPickData*, s_instance, 401585);
    return *s_instance.Get();
}
