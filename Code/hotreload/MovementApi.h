#pragma once

#include <stdint.h>

// Versioned C ABI. No STL, engine pointers, allocations, callbacks, threads or
// persistent state may cross this boundary. Calls are synchronous; inputs and
// outputs are borrowed for the duration of Evaluate only. Changing this layout
// requires an ABI bump AND a restart of the resident plugin.
#define COOP_MOVEMENT_ABI 1u

typedef struct CoopMovementInput
{
    uint64_t tick;
    uint64_t first_tick;
    uint64_t second_tick;
    float first_position[3];
    float second_position[3];
    float first_rotation[3];
    float second_rotation[3];
} CoopMovementInput;

typedef struct CoopMovementOutput
{
    float position[3];
    float rotation[3];
} CoopMovementOutput;

typedef struct CoopMovementApi
{
    uint32_t version;
    uint32_t size;
    char build_id[64];
    void (*evaluate)(const CoopMovementInput*, CoopMovementOutput*);
} CoopMovementApi;

typedef int (*CoopGetMovementApi)(uint32_t, uint32_t, CoopMovementApi*);
