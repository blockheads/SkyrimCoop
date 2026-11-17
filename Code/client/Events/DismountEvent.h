#pragma once

#include <cstdint>

// One of this game's actors is off its mount: its interaction ended (RefrInteraction::Detach 41539), by any path.
struct DismountEvent
{
    uint32_t RiderId;
    uint32_t MountId{}; // 0: gone already (its Predestroy)
};
