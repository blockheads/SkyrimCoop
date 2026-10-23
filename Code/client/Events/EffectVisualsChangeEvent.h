#pragma once

#include <cstdint>

/**
 * @brief The engine started or stopped an effect's hit visuals on an actor (EffectEvents).
 */
struct EffectVisualsChangeEvent
{
    uint32_t ActorId{};
    // The effect whose visuals just started and play once (no FX Persist); 0: none.
    uint32_t FlashEffectId{};
};
