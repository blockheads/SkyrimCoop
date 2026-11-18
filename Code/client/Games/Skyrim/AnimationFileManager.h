#pragma once

#include <Games/Primitives.h>

// A queued clip-file load (63969/63968, 0x68): the requester's lock is taken before its callback runs (63997), so it
// must outlive the load: a cancel (63971) comes too late once 63997 has taken it. docs/re/systems/animation-file-loads.md
struct AnimationFileLoad
{
    volatile LONG refCount; // 00
    uint8_t pad04[0x38 - 0x04];
    uint32_t kind; // 38, the clip set's (3 action, 4 instant, 47 a movement idle's)
    uint8_t pad3C[0x58 - 0x3C];
    BSRecursiveLock* lock; // 58, the requester's
    bool live;             // 60, cleared by a cancel (63986)
};
static_assert(offsetof(AnimationFileLoad, kind) == 0x38);
static_assert(offsetof(AnimationFileLoad, lock) == 0x58);
static_assert(offsetof(AnimationFileLoad, live) == 0x60);

// AnimationFileManagerSingleton (401579): its loads waiting (a max-heap by priority), the one under way, and the
// finished ones whose callbacks its job (63980) runs next.
struct AnimationFileManager
{
    struct Pending
    {
        uint32_t priority;
        AnimationFileLoad* load;
    };

    uint8_t pad0[0x0C];
    BSRecursiveLock lock; // 0C, guards the three below
    uint8_t pad14[0x68 - 0x14];
    GameArray<Pending> pending;            // 68 (64045, 64014)
    AnimationFileLoad* loading;            // 80
    GameArray<AnimationFileLoad*> finished; // 88

    static AnimationFileManager* Get() noexcept;
};
static_assert(offsetof(AnimationFileManager, lock) == 0x0C);
static_assert(offsetof(AnimationFileManager, pending) == 0x68);
static_assert(offsetof(AnimationFileManager, pending.length) == 0x78);
static_assert(offsetof(AnimationFileManager, loading) == 0x80);
static_assert(offsetof(AnimationFileManager, finished) == 0x88);
static_assert(offsetof(AnimationFileManager, finished.length) == 0x98);
