#include <TiltedOnlinePCH.h>

#include <Camera/VATS.h>

VATS* VATS::Get() noexcept
{
    POINTER_SKYRIMSE(VATS*, s_instance, 400883);
    return *s_instance.Get();
}
