#pragma once

// What the player's crosshair points at (CommonLibSSE-NG CrosshairPickData, singleton 401585).
struct CrosshairPickData
{
    static CrosshairPickData* Get() noexcept;

    uint32_t pad00;
    uint32_t targetHandle;      // any reference
    uint32_t targetActorHandle; // an actor (what a player's attack aims at)
};
static_assert(offsetof(CrosshairPickData, targetHandle) == 0x4);
static_assert(offsetof(CrosshairPickData, targetActorHandle) == 0x8);
