#pragma once

#include <cstdint>

/**
 * @brief A reference's 3D loaded in this game (TESObjectLoadedEvent): visuals played on it before are gone.
 */
struct ReferenceLoadedEvent
{
    uint32_t FormId{};
};
