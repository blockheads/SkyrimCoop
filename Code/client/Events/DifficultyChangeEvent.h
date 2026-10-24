#pragma once

#include <cstdint>

// This game's engine set its difficulty: the settings menu, its defaults, the saved settings applied.
struct DifficultyChangeEvent
{
    uint32_t Difficulty;
};
