#include "BGSCreatedObjectManager.h"
#include <VersionDb.h>

BGSCreatedObjectManager* BGSCreatedObjectManager::Get() noexcept
{
    POINTER_SKYRIMSE(BGSCreatedObjectManager*, pObjManager, 400320);
    return *pObjManager.Get();
}
