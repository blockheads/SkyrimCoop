#pragma once

#include <Messages/NotifyCharacterSpawn.h>

struct WaitingFor3D
{
    WaitingFor3D(const NotifyCharacterSpawn& aSpawnRequest)
        : SpawnRequest(aSpawnRequest)
    {
    }

    NotifyCharacterSpawn SpawnRequest{};
};
