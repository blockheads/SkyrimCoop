#include <Misc/GameVM.h>
#include <VersionDb.h>

SkyrimVM* SkyrimVM::Get()
{
    POINTER_SKYRIMSE(SkyrimVM*, s_instance, 400475);

    return *s_instance.Get();
}
