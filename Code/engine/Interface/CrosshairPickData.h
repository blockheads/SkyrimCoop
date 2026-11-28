#pragma once

// What the player's crosshair points at (CommonLibSSE-NG CrosshairPickData, singleton 401585).
struct CrosshairPickData
{
    static CrosshairPickData* Get() noexcept;

    uint32_t pad00;
    uint32_t targetHandle;      // any reference
    uint32_t targetActorHandle; // an actor (what a player's attack aims at)
    uint32_t grabPickRef;       // 0C
    float collisionPoint[3];    // 10: where its ray hit (the grab's anchor, 40555)
    uint32_t pad1C;
    struct bhkWorldObject* targetCollider; // 20: the body its ray hit, what a grab springs (40555)
};
static_assert(offsetof(CrosshairPickData, targetHandle) == 0x4);
static_assert(offsetof(CrosshairPickData, targetActorHandle) == 0x8);
static_assert(offsetof(CrosshairPickData, collisionPoint) == 0x10);
static_assert(offsetof(CrosshairPickData, targetCollider) == 0x20);
