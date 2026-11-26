#include <DefaultObjectManager.h>
#include <VersionDb.h>

DefaultObjectManager& DefaultObjectManager::Get()
{
    using TGetDefaultObjectManager = DefaultObjectManager&();

    POINTER_SKYRIMSE(TGetDefaultObjectManager, GetDefaultObjectManager, 13894);

    return GetDefaultObjectManager();
}

TESForm* DefaultObjectManager::GetObject(Object aObject) noexcept
{
    using TGetObject = TESForm*(uint32_t);
    POINTER_SKYRIMSE(TGetObject, getObject, 11436);
    return getObject(aObject);
}
