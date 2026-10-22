#include <TiltedOnlinePCH.h>
#include <HookSwitch.h>
#include <HeapHistory.h>

#include <Games/Memory.h>
#include <Games/References.h>

#include <rpmalloc.h>

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

static bool IsFormAllocateReplacedByEF(TFormAllocate** appOutEngineFixesAlloc) noexcept
{
    POINTER_SKYRIMSE(TFormAllocate, s_formAllocate, 68115);
    TFormAllocate* pFormAllocate = s_formAllocate.Get();

    auto opcodeBytes = reinterpret_cast<uint16_t*>(*pFormAllocate);
    uint8_t shift = 0;

    if (*opcodeBytes == 0x25FF) // 'jmp' opcode 'FF 25' and the 4-byte displacement bytes come before the virtual address we're after
        shift = 6;
    else if (*opcodeBytes == 0xB848) // 'mov' opcode '48 B8' comes before the virtual address we're after
        shift = 2;

    auto possibleEfAllocAddress = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(*pFormAllocate) + shift);

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)possibleEfAllocAddress, &mbi, sizeof(mbi)) != 0)
    {
        if (mbi.AllocationBase == GetModuleHandleW(L"EngineFixes.dll"))
        {
            *appOutEngineFixesAlloc = reinterpret_cast<TFormAllocate*>(possibleEfAllocAddress);
            return true;
        }
    }
    return false;
}

static void RehookFormAllocate(TFormAllocate* apEngineFixesAllocate) noexcept
{
    RealFormAllocate = apEngineFixesAllocate;
    TP_HOOK_IMMEDIATE(&RealFormAllocate, HookFormAllocate);
}

size_t Hook_msize(void* apData)
{
    return rpmalloc_usable_size(apData);
}

void Hookfree(void* apData)
{
    rpfree(apData);
}

void* Hookcalloc(size_t aCount, size_t aSize)
{
    return rpcalloc(aCount, aSize);
}

void* Hookmalloc(size_t aSize)
{
    return rpmalloc(aSize);
}

void Hook_aligned_free(void* apData)
{
    rpfree(apData);
}

void* Hook_aligned_malloc(size_t aSize, size_t aAlignment)
{
    return rpmemalign(aAlignment, aSize);
}

static TiltedPhoques::Initializer s_memoryHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TFormAllocate, s_formAllocate, 68115);

        POINTER_SKYRIMSE(TFormFree, s_formFree, 68117);
        POINTER_SKYRIMSE(TFormReallocate, s_formReallocate, 68116);

        RealFormAllocate = s_formAllocate.Get();
        RealFormFree = s_formFree.Get();
        RealFormReallocate = s_formReallocate.Get();

#ifdef TP_ORIGINAL_LAUNCHER // never defined: SKSE loads us too late to swap the CRT heap
        // The original launcher installs these before the game allocates.
        // SKSE loads us after CRT startup: replacing malloc/free then would mix
        // existing game allocations with rpmalloc and uninitialized engine
        // threads. Keep the game's CRT allocator in the SKSE build.
        using T_msize = decltype(&Hook_msize);
        using Tfree = decltype(&Hookfree);
        using Tcalloc = decltype(&Hookcalloc);
        using Tmalloc = decltype(&Hookmalloc);
        using T_aligned_malloc = decltype(&Hook_aligned_malloc);
        using T_aligned_free = decltype(&Hook_aligned_free);
        T_msize Real_msize = nullptr;
        Tfree Realfree = nullptr;
        Tcalloc Realcalloc = nullptr;
        Tmalloc Realmalloc = nullptr;
        T_aligned_malloc Real_aligned_malloc = nullptr;
        T_aligned_free Real_aligned_free = nullptr;

        const char* cModuleName = "api-ms-win-crt-heap-l1-1-0.dll";

        TP_HOOK_IAT(_msize, cModuleName);
        TP_HOOK_IAT(free, cModuleName);
        TP_HOOK_IAT(calloc, cModuleName);
        TP_HOOK_IAT(malloc, cModuleName);
        TP_HOOK_IAT(_aligned_malloc, cModuleName);
        TP_HOOK_IAT(_aligned_free, cModuleName);
#endif

        TP_HOOK(&RealFormAllocate, HookFormAllocate);
        if (HeapHistory::Start())
        {
            TP_HOOK(&RealFormFree, HookFormFree);
            TP_HOOK(&RealFormReallocate, HookFormReallocate);
        }
    });

using T_initterm_e = decltype(&_initterm_e);
T_initterm_e Real_initterm_e = nullptr;

// If EngineFixes loaded, and it changed our FormAllocate hook,
// reset it. Our hook works just fine chaining to theirs.
int __cdecl Hook_initterm_e(_PIFV* apFirst, _PIFV* apLast)
{
    // We want to run last, so pre-chain.
    auto retval = Real_initterm_e(apFirst, apLast);

    // Check if EngineFixes messed with STR's modified alloc hook; if it did, treat EF as truth and rehook
    TFormAllocate* pEngineFixesAllocate = nullptr;
    if (GetModuleHandleW(L"EngineFixes.dll") && IsFormAllocateReplacedByEF(&pEngineFixesAllocate))
        RehookFormAllocate(pEngineFixesAllocate);

    return retval;
}

void HookFormAllocateSentinelInit()
{
    TP_HOOK_IAT(_initterm_e, "api-ms-win-crt-runtime-l1-1-0.dll");
}

#pragma optimize("", on)
