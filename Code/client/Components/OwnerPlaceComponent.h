#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/GameId.h>

// Where a copy's owner last said it is (its movement and spawns), for the presence check
// (CharacterService::RunPresence); LastMove spaces out the moves it makes.
struct OwnerPlaceComponent
{
    GameId CellId{};
    GameId WorldSpaceId{};
    std::chrono::steady_clock::time_point LastMove{};
};
