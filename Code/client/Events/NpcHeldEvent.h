#pragma once

#include <cstdint>

/**
 * @brief A guest's own NPC was refused its 3D (GuestNpcs): done loading without it, as a disabled reference.
 */
struct NpcHeldEvent
{
    uint32_t FormId{};
};
