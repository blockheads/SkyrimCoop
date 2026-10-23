#pragma once

#include <cstdint>

/**
 * @brief An actor's factions or ranks may have changed in this game (FactionEvents).
 */
struct FactionsChangeEvent
{
    uint32_t ActorId{};
};
