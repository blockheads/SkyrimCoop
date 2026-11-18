#pragma once

#include <cstdint>

// An actor is off its mount: its interaction ended (RefrInteraction::Detach 41539), by any path.
struct DismountEvent
{
    uint32_t RiderId;
    uint32_t MountId{}; // 0: gone already (its Predestroy)
    bool Copy{};        // a copy's: its mount's slot emptied all the same
};
