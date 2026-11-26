#include <DefaultStringManager.h>
#include <VersionDb.h>

DefaultStringManager& DefaultStringManager::Get()
{
    using TGetDefaultStringManager = DefaultStringManager&();

    POINTER_SKYRIMSE(TGetDefaultStringManager, GetDefaultStringManager, 11437);

    return GetDefaultStringManager();
}
