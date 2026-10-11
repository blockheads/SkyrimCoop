#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Messages/CharacterSpawnRequest.h>

// On a copy of another player's character: what it was spawned from. The host sends it again to a
// player who joins later, with the copy's live state (CharacterService::BuildSpawnRequests): the
// appearance and base cannot be read back from the copy.
struct SpawnRecordComponent
{
    CharacterSpawnRequest Spawn;
};
