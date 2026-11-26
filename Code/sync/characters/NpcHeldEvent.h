#pragma once

#include <cstdint>

/**
 * @brief A guest's own NPC's load job ended with its 3D refused (GuestNpcs): done loading without it.
 */
struct NpcHeldEvent
{
    uint32_t FormId{};
};
