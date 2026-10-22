#include <Interface/MenuCursor.h>
#include <VersionDb.h>

MenuCursor* MenuCursor::Get() noexcept
{
    POINTER_SKYRIMSE(MenuCursor*, s_instance, 403551);
    return *s_instance.Get();
}
