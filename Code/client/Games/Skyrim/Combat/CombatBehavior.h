#pragma once

#include <Games/Primitives.h>
#include <Misc/BSFixedString.h>

// The combat AI's running tree and state, read by the harness (CommonLibSSE-NG, checked in IDA for 1.7.104).
struct CombatBehaviorTreeNode
{
    void* pVtable;
    BSFixedString name;
};
static_assert(offsetof(CombatBehaviorTreeNode, name) == 0x8);

struct CombatBehaviorThread
{
    struct Frame
    {
        const CombatBehaviorTreeNode* pNode;
        uint32_t stackOffset;
    };
    static_assert(sizeof(Frame) == 0x10);

    uint8_t stack[0x10];
    uint32_t frameCapacity; // top bit: the frames are local
    uint32_t pad14;
    union
    {
        Frame localFrames[16];
        Frame* pHeapFrames;
    };
    uint32_t frameCount;
    uint32_t pad11C;
    uint32_t stackPosition;
};
static_assert(offsetof(CombatBehaviorThread, localFrames) == 0x18);
static_assert(offsetof(CombatBehaviorThread, frameCount) == 0x118);
static_assert(offsetof(CombatBehaviorThread, stackPosition) == 0x120);

struct CombatBehaviorController
{
    CombatBehaviorThread* pDefaultThread;
    GameArray<CombatBehaviorThread*> activeThreads;
};
static_assert(offsetof(CombatBehaviorController, activeThreads) == 0x8);

struct CombatState
{
    float unk00;
    bool isFleeing;
    bool isDetected;
    bool targetLost;
    uint8_t pad07[0x28 - 0x07];
    float lastAttackedTime; // 28: set by Actor::DoDamage and the attacked handler
    uint8_t pad2C[0x80 - 0x2C];
    GameArray<uint8_t> unreachableLocations; // 0x20 each: UnreachableLocation
};
static_assert(offsetof(CombatState, targetLost) == 0x6);
static_assert(offsetof(CombatState, lastAttackedTime) == 0x28);
static_assert(offsetof(CombatState, unreachableLocations) == 0x80);

// The path follower's query interface (IMovementQueryPathFollowing, MovementAgentPathFollowerStandard+0x28): 90724 reads
// these two; a path is followed in states 1-8 and 13.
struct MovementPath
{
    uint8_t pad0[0xA0];
    uint32_t pointCount;
};
static_assert(offsetof(MovementPath, pointCount) == 0xA0);

struct MovementQueryPathFollowing
{
    uint8_t pad0[0xF0];
    uint32_t state;
    uint8_t padF4[0x100 - 0xF4];
    const MovementPath* pPath;
};
static_assert(offsetof(MovementQueryPathFollowing, state) == 0xF0);
static_assert(offsetof(MovementQueryPathFollowing, pPath) == 0x100);

// A movement controller's first vtable slots (MovementControllerNPC, CommonLibSSE-NG): slot 3 finds an interface by
// its type id, as Actor::GetPredictedPosition (47309) asks for IMovementQueryPathFollowing (type id 412229).
struct MovementControllerVtable
{
    void* slots[3];
    void* (*QueryInterface)(void* apController, const void* apTypeId);
};
static_assert(offsetof(MovementControllerVtable, QueryInterface) == 0x18);

struct MovementController
{
    const MovementControllerVtable* pVtable;
};
