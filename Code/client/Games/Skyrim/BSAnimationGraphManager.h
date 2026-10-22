#pragma once

#include <Games/Primitives.h>

#include <Havok/BShkbAnimationGraph.h>

struct BSFixedString;

struct BSAnimationGraphManager
{
    virtual ~BSAnimationGraphManager();
    virtual void sub_1(void* apUnk1);

    void Release()
    {
        if (InterlockedDecrement(&refCount) == 0)
            this->~BSAnimationGraphManager();
    }

    volatile LONG refCount;
    // The channels feeding the graphs (CommonLib).
    GameArray<struct BSAnimationGraphChannel*> boundChannels; // 10
    GameArray<struct BSAnimationGraphChannel*> bumpedChannels; // 28
    BSTSmallArray<BShkbAnimationGraph> animationGraphs; // 40 - 20
    void* pad_ptrs2[9];
    BSRecursiveLock lock;  // 98 - 4C
    void* unkPtrAfterLock; // A0 - 58


    uint32_t animationGraphIndex; // A8 - 5C

    std::map<uint32_t, std::string> DumpAnimationVariables(bool aPrintVariables);
    uint64_t GetDescriptorKey(int aForceIndex = -1);
    uint32_t ReSendEvent(BSFixedString* apEventName);
};

static_assert(offsetof(BSAnimationGraphManager, boundChannels) == 0x10);
static_assert(offsetof(BSAnimationGraphManager, animationGraphs) == 0x40);
static_assert(offsetof(BSAnimationGraphManager, lock) == 0xA0);
static_assert(offsetof(BSAnimationGraphManager, animationGraphIndex) == 0xB0);
