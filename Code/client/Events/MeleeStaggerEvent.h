#pragma once

#include <cstdint>

// A local player's melee hit selected a stagger on another game's actor; its owner applies the reaction.
struct MeleeStaggerEvent
{
    uint32_t HitteeId{};
    uint32_t HitterId{};
    float Magnitude{};
};
