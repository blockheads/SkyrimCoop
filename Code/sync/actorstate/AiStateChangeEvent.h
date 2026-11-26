#pragma once

#include <cstdint>

/**
 * @brief An actor's AI state may have changed in this game: its package, combat state, death or a scene (0: any actor).
 */
struct AiStateChangeEvent
{
    uint32_t ActorId{};
};
