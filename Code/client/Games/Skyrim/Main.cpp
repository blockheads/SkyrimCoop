#include <Games/Skyrim/Main.h>

Main* Main::Get() noexcept
{
    POINTER_SKYRIMSE(Main*, s_singleton, 403449);
    return *s_singleton.Get();
}
