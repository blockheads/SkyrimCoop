#pragma once

// What the player's crosshair points at (CommonLibSSE-NG CrosshairPickData, singleton 401585).
struct CrosshairPickData
{
    static CrosshairPickData* Get() noexcept;

    uint32_t pad00;
    uint32_t targetHandle;      // any reference
    uint32_t targetActorHandle; // an actor (what a player's attack aims at)
};
SKYRIM_STRUCT_ASSERT(offsetof(CrosshairPickData, targetHandle) == 0x4);
SKYRIM_STRUCT_ASSERT(offsetof(CrosshairPickData, targetActorHandle) == 0x8);
