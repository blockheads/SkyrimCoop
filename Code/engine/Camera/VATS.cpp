#include <Camera/VATS.h>
#include <VersionDb.h>

VATS* VATS::Get() noexcept
{
    POINTER_SKYRIMSE(VATS*, s_instance, 400883);
    return *s_instance.Get();
}

void VATS::End() noexcept
{
    using TSetMode = void(VATS*, Mode);
    POINTER_SKYRIMSE(TSetMode, s_setMode, 44282);
    s_setMode.Get()(this, kNone);
}
