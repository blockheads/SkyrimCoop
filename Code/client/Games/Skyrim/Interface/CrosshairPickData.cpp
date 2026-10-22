#include <Interface/CrosshairPickData.h>
#include <VersionDb.h>

CrosshairPickData* CrosshairPickData::Get() noexcept
{
    POINTER_SKYRIMSE(CrosshairPickData*, s_instance, 401585);
    return *s_instance.Get();
}
