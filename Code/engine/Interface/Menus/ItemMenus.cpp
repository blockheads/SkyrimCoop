#include <Interface/Menus/ItemMenus.h>
#include <VersionDb.h>

uint32_t ContainerMenu::GetTargetRefHandle() noexcept
{
    POINTER_SKYRIMSE(uint32_t, s_handle, 405962);
    return *s_handle.Get();
}

uint32_t BarterMenu::GetTargetRefHandle() noexcept
{
    POINTER_SKYRIMSE(uint32_t, s_handle, 405823);
    return *s_handle.Get();
}
