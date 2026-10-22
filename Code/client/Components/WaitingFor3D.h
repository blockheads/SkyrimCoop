#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Messages/NotifyCharacterSpawn.h>

struct WaitingFor3D
{
    WaitingFor3D(const NotifyCharacterSpawn& aSpawnRequest)
        : SpawnRequest(aSpawnRequest)
    {
    }

    NotifyCharacterSpawn SpawnRequest{};
};
