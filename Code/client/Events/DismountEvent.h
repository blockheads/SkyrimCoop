#pragma once

#include <cstdint>

// One of this game's actors started getting off its mount (Actor::Dismount, 37906).
struct DismountEvent
{
    uint32_t RiderId;
};
