#pragma once

#include <cstddef>
#include <cstdint>

// An NPC's movement controller (Actor+0x150, CommonLib MovementControllerNPC). docs/re/systems/actor-movement.md,
// "Motion-driven and the Virtual Path Follower".
struct MovementControllerNPC
{
    uint8_t pad0[0x1C0];
    // Its movement mode: 0/4/5/9 animation-driven (41707), 10/11 the Virtual Path Follower (41703).
    uint32_t movementState;
    uint8_t pad1C4[4];
    bool agentsChosen;  // 1C8: an agent set was picked (41701/41702)
    bool loadedAgents;  // 1C9: the standard agents (41701), not the virtual set (41702)
    uint8_t pad1CA[6];
};
static_assert(offsetof(MovementControllerNPC, movementState) == 0x1C0);
static_assert(offsetof(MovementControllerNPC, loadedAgents) == 0x1C9);
static_assert(sizeof(MovementControllerNPC) == 0x1D0);
