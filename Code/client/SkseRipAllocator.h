#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace CoopSkse
{
// The original launcher reserved a nearby executable highrip section. An SKSE
// DLL can be loaded far outside rel32 reach, so its .bss cannot replace highrip.
struct JitPool
{
    static constexpr size_t kCapacity = 1024 * 1024;
    uint8_t* Memory = nullptr;
    size_t Used = 0;
    bool Reserved = false;
    std::mutex Mutex;
};

inline JitPool& GetJitPool()
{
    static JitPool s_pool;
    return s_pool;
}

// First free region of `capacity` bytes in [aFrom, aTo), walking up.
inline uint8_t* ReserveFreeRegion(uintptr_t aFrom, uintptr_t aTo, size_t aCapacity)
{
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto granularity = static_cast<uintptr_t>(system.dwAllocationGranularity);
    auto cursor = (aFrom + granularity - 1) & ~(granularity - 1);
    while (cursor + aCapacity <= aTo)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)))
            break;
        const auto end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (region.State == MEM_FREE && end - cursor >= aCapacity)
        {
            if (void* memory = VirtualAlloc(reinterpret_cast<void*>(cursor), aCapacity, MEM_RESERVE | MEM_COMMIT,
                                            PAGE_EXECUTE_READWRITE))
                return static_cast<uint8_t*>(memory);
        }
        if (end <= cursor)
            break;
        cursor = (end + granularity - 1) & ~(granularity - 1);
    }
    return nullptr;
}

inline void* AllocateJit(size_t bytes)
{
    constexpr size_t alignment = 16;
    JitPool& pool = GetJitPool();
    std::lock_guard<std::mutex> lock(pool.Mutex);
    if (!pool.Reserved)
    {
        pool.Reserved = true;
        const auto game = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // All candidates are within 1.75 GiB of the image base, leaving room for relative
        // jumps from the start and end of the game's image. Above first, then below.
        pool.Memory = ReserveFreeRegion(game, game + 0x70000000, JitPool::kCapacity);
        if (!pool.Memory && game > 0x70000000)
            pool.Memory = ReserveFreeRegion(game - 0x70000000, game, JitPool::kCapacity);
    }
    if (!pool.Memory || !bytes || bytes > JitPool::kCapacity - pool.Used)
        return nullptr;
    const auto aligned = (bytes + alignment - 1) & ~(alignment - 1);
    if (aligned > JitPool::kCapacity - pool.Used)
        return nullptr;
    void* result = pool.Memory + pool.Used;
    pool.Used += aligned;
    return result;
}

// Hot reload: the plugin unloads after restoring every patch that branches into the pool, and the
// next load reserves its own (repeated reloads would otherwise use up the space near the game).
inline void ReleaseJit()
{
    JitPool& pool = GetJitPool();
    std::lock_guard<std::mutex> lock(pool.Mutex);
    if (pool.Memory)
        VirtualFree(pool.Memory, 0, MEM_RELEASE);
    pool.Memory = nullptr;
    pool.Used = 0;
}
}
