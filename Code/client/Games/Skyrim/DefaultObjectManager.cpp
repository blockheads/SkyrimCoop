#include <DefaultObjectManager.h>
#include <VersionDb.h>

DefaultObjectManager& DefaultObjectManager::Get()
{
    using TGetDefaultObjectManager = DefaultObjectManager&();

    POINTER_SKYRIMSE(TGetDefaultObjectManager, GetDefaultObjectManager, 13894);

    return GetDefaultObjectManager();
}
