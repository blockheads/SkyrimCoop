#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace CoopSkse
{
// The original launcher reserved a nearby executable highrip section. An SKSE
// DLL can be loaded far outside rel32 reach, so its .bss cannot replace highrip.
inline void* AllocateJit(size_t bytes)
{
    constexpr size_t capacity = 1024 * 1024;
    constexpr size_t alignment = 16;
    static auto* pool = []() -> uint8_t* {
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        const auto granularity = static_cast<uintptr_t>(system.dwAllocationGranularity);
        const auto game = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // All candidates are within +1.75 GiB of the image base, leaving room
        // for relative jumps from the start and end of the game's image.
        const auto limit = game + 0x70000000;
        auto cursor = game;
        while (cursor + capacity <= limit)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)))
                break;
            const auto end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
            if (region.State == MEM_FREE && end - cursor >= capacity)
            {
                if (void* memory = VirtualAlloc(reinterpret_cast<void*>(cursor), capacity,
                                               MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                    return static_cast<uint8_t*>(memory);
            }
            if (end <= cursor) break;
            cursor = (end + granularity - 1) & ~(granularity - 1);
        }
        return nullptr;
    }();
    static std::mutex mutex;
    static size_t used = 0;
    std::lock_guard<std::mutex> lock(mutex);
    if (!pool || !bytes || bytes > capacity - used) return nullptr;
    const auto aligned = (bytes + alignment - 1) & ~(alignment - 1);
    if (aligned > capacity - used) return nullptr;
    void* result = pool + used;
    used += aligned;
    return result;
}
}
