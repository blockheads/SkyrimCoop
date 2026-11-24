#include <Magic/MagicFavorites.h>
#include <VersionDb.h>

MagicFavorites* MagicFavorites::Get() noexcept
{
    POINTER_SKYRIMSE(MagicFavorites*, s_instance, 403337);
    return *(s_instance.Get());
}
