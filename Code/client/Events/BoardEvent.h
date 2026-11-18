#pragma once

#include <cstdint>

// Our player activated a mount someone already rides (Character::ActivateMount 37864 does nothing then): a passenger
// if a party player drives it (CharacterService).
struct BoardEvent
{
    uint32_t RiderId;
    uint32_t MountId;
};
