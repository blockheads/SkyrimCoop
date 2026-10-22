#include <ClientPch.h>
#include <HookSwitch.h>
#include <HeapHistory.h>

#include <Games/Memory.h>
#include <Games/References.h>

#pragma optimize("", off)

struct GameHeap
{
    static GameHeap* Get()
    {
        POINTER_SKYRIMSE(GameHeap, s_gameHeap, 400188);

        return s_gameHeap.Get();
    }
};

using TFormAllocate = void*(GameHeap*, size_t aSize, size_t aAlignment, bool aAligned);
using TFormFree = void(GameHeap*, void* apPtr, bool aAligned);
using TFormReallocate = void*(GameHeap*, void* apOld, size_t aSize, uint32_t aAlignment, bool aAligned);

TFormAllocate* RealFormAllocate = nullptr;
TFormFree* RealFormFree = nullptr;
TFormReallocate* RealFormReallocate = nullptr;

void* HookFormAllocate(GameHeap* apThis, size_t aSize, size_t aAlignment, bool aAligned)
{
    switch (aSize)
    {
    case sizeof(Actor): aSize = sizeof(ExActor); break;
    case sizeof(PlayerCharacter): aSize = sizeof(ExPlayerCharacter); break;
    default: break;
    }

    void* pPointer = HeapHistory::Start() ? HeapHistory::GuardAllocate(aSize) : nullptr;
    if (!pPointer)
        pPointer = RealFormAllocate(apThis, aSize, aAlignment, aAligned);

    if (!pPointer)
        return nullptr;

    ActorExtension* pExtension = nullptr;

    switch (aSize)
    {
    case sizeof(ExActor): pExtension = static_cast<ActorExtension*>(static_cast<ExActor*>(pPointer)); break;
    case sizeof(ExPlayerCharacter): pExtension = static_cast<ActorExtension*>(static_cast<ExPlayerCharacter*>(pPointer)); break;
    default: break;
    }

    if (pExtension)
    {
        new (pExtension) ActorExtension;
    }

    if (HeapHistory::Start())
        HeapHistory::OnAllocate(pPointer, aSize);

    return pPointer;
}

// Only hooked with the heap history on (HeapHistory.h); our own frees always come through here.
void HookFormFree(GameHeap* apThis, void* apPtr, bool aAligned)
{
    if (apPtr && HeapHistory::Start())
    {
        HeapHistory::OnFree(apPtr);
        if (HeapHistory::GuardFree(apPtr))
            return;
        if (HeapHistory::Quarantine(apPtr, aAligned))
        {
            void* pEvicted = nullptr;
            bool evictedAligned = false;
            while (HeapHistory::TakeEvicted(pEvicted, evictedAligned))
                RealFormFree(apThis, pEvicted, evictedAligned);
            return;
        }
    }
    RealFormFree(apThis, apPtr, aAligned);
}

// Only hooked with the heap guard on: the engine's own cannot size a guarded block.
void* HookFormReallocate(GameHeap* apThis, void* apOld, size_t aSize, uint32_t aAlignment, bool aAligned)
{
    const size_t cOldSize = apOld ? HeapHistory::GuardedSize(apOld) : 0;
    if (!cOldSize)
        return RealFormReallocate(apThis, apOld, aSize, aAlignment, aAligned);
    if (!aSize)
        return nullptr; // as the engine's: a zero size allocates nothing and keeps the old block
    void* pNew = HookFormAllocate(apThis, aSize, aAlignment, aAligned);
    if (pNew)
        std::memcpy(pNew, apOld, std::min(aSize, cOldSize));
    HookFormFree(apThis, apOld, aAligned);
    return pNew;
}

void* Memory::Allocate(const size_t aSize) noexcept
{
    return HookFormAllocate(GameHeap::Get(), aSize, 0, false);
}

void Memory::Free(void* apData) noexcept
{
    HookFormFree(GameHeap::Get(), apData, false);
}

static Hooks::Initializer s_memoryHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TFormAllocate, s_formAllocate, 68115);

        POINTER_SKYRIMSE(TFormFree, s_formFree, 68117);
        POINTER_SKYRIMSE(TFormReallocate, s_formReallocate, 68116);

        RealFormAllocate = s_formAllocate.Get();
        RealFormFree = s_formFree.Get();
        RealFormReallocate = s_formReallocate.Get();

        HOOK(&RealFormAllocate, HookFormAllocate);
        if (HeapHistory::Start())
        {
            HOOK(&RealFormFree, HookFormFree);
            HOOK(&RealFormReallocate, HookFormReallocate);
        }
    });

#pragma optimize("", on)
