#include <ClientPch.h>

#include <Interface/MenuCursor.h>

MenuCursor* MenuCursor::Get() noexcept
{
    POINTER_SKYRIMSE(MenuCursor*, s_instance, 403551);
    return *s_instance.Get();
}
