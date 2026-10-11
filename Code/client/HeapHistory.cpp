#include "HeapHistory.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <unordered_map>

namespace
{
constexpr int cFrames = 12;

struct Event
{
    uintptr_t Block;
    uint32_t Size; // an allocation's; 0 for a free
    uint32_t ThreadId;
    uint64_t Ms;
    void* Frames[cFrames];
};

// The ring: every event, the newest s_capacity of them.
Event* s_events = nullptr;
size_t s_capacity = 0;
std::atomic<uint64_t> s_next{0};

// Watched addresses: every event of the blocks holding them, since boot.
constexpr size_t cMaxWatches = 16;
constexpr size_t cMaxWatched = 4096;
uintptr_t s_watches[cMaxWatches];
size_t s_watchCount = 0;
std::atomic<uintptr_t> s_watchBlocks[cMaxWatches]; // the block holding each now (0: none)
struct WatchedEvent
{
    size_t Watch;
    Event Event;
};
WatchedEvent* s_watched = nullptr;
std::atomic<size_t> s_watchedNext{0};

// The quarantine: freed blocks are filled with cPoison and held back from reuse until
// s_quarantineLimit bytes are held, so a read through a stale pointer gets a value that faults at
// once (non-canonical), while what it read from is still in a register and in the history.
constexpr uint8_t cPoison = 0xDB; // 0xDBDBDBDBDBDBDBDB as a pointer
size_t s_quarantineLimit = 0;

struct SpinLock
{
    std::atomic_flag Flag = ATOMIC_FLAG_INIT;
    void lock() noexcept
    {
        while (Flag.test_and_set(std::memory_order_acquire))
            YieldProcessor();
    }
    void unlock() noexcept { Flag.clear(std::memory_order_release); }
};

// Sizes of the blocks allocated since our hooks (the engine's allocator has no size query); the
// quarantine only takes those. In the CRT heap, not the engine's.
constexpr size_t cStripes = 256;
struct SizeStripe
{
    SpinLock Lock;
    std::unordered_map<uintptr_t, uint32_t> Sizes;
};
SizeStripe* s_sizeStripes = nullptr;
SizeStripe& StripeOf(uintptr_t aBlock) noexcept
{
    return s_sizeStripes[(aBlock >> 4) * 0x9E3779B97F4A7C15ull >> 56];
}

// The guard (page heap): blocks of the guarded sizes get pages of their own, made inaccessible on
// free and never reused, so any later access faults at the instruction that makes it.
constexpr size_t cMaxGuardSizes = 8;
uint32_t s_guardSizes[cMaxGuardSizes];
size_t s_guardSizeCount = 0;
SpinLock s_guardLock;
std::unordered_map<uintptr_t, uint32_t> s_guardLive;  // block -> size
std::unordered_map<uintptr_t, uint32_t> s_guardFreed; // never reused: still answers "whose was this"
constexpr size_t cPage = 0x1000;

struct Held
{
    void* Block;
    uint32_t Size;
    bool Aligned;
};
SpinLock s_heldLock;
std::deque<Held> s_held;
size_t s_heldBytes = 0;

void Capture(Event& aEvent, uintptr_t aBlock, uint32_t aSize) noexcept
{
    aEvent.Block = aBlock;
    aEvent.Size = aSize;
    aEvent.ThreadId = GetCurrentThreadId();
    aEvent.Ms = GetTickCount64();
    // Skips Capture and Add; crash_report.py leaves out the allocator hook's own frames.
    const USHORT cCaptured = RtlCaptureStackBackTrace(2, cFrames, aEvent.Frames, nullptr);
    for (int i = cCaptured; i < cFrames; ++i)
        aEvent.Frames[i] = nullptr;
}

void Add(const void* apBlock, uint32_t aSize) noexcept
{
    const auto cBlock = reinterpret_cast<uintptr_t>(apBlock);
    Event* pCaptured = nullptr;
    Event local;
    if (s_events)
    {
        pCaptured = &s_events[s_next.fetch_add(1, std::memory_order_relaxed) % s_capacity];
        Capture(*pCaptured, cBlock, aSize);
    }
    for (size_t watch = 0; watch < s_watchCount; ++watch)
    {
        // An allocation covering the address, or the free of the block that held it.
        const bool cAllocated = aSize && s_watches[watch] >= cBlock && s_watches[watch] - cBlock < aSize;
        const bool cFreed = !aSize && s_watchBlocks[watch].load(std::memory_order_relaxed) == cBlock;
        if (!cAllocated && !cFreed)
            continue;
        s_watchBlocks[watch].store(cAllocated ? cBlock : 0, std::memory_order_relaxed);
        if (!pCaptured)
        {
            Capture(local, cBlock, aSize);
            pCaptured = &local;
        }
        const size_t cIndex = s_watchedNext.fetch_add(1, std::memory_order_relaxed);
        if (cIndex < cMaxWatched)
            s_watched[cIndex] = {watch, *pCaptured};
    }
}

void LogFrames(const Event& acEvent) noexcept
{
    for (void* pFrame : acEvent.Frames)
    {
        if (!pFrame)
            break;
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(pFrame), &module);
        char path[MAX_PATH] = "?";
        if (module)
            GetModuleFileNameA(module, path, MAX_PATH);
        spdlog::error("      backtrace #00 {}+{:#x}", std::filesystem::path(path).filename().string(),
                      reinterpret_cast<uintptr_t>(pFrame) - reinterpret_cast<uintptr_t>(module));
    }
}

void LogEvent(const char* acpWhat, const Event& acEvent, uint64_t aNow) noexcept
{
    if (acEvent.Size)
        spdlog::error("    {} {:#x} bytes at {:#x}, {} ms before the crash, thread {}:", acpWhat, acEvent.Size, acEvent.Block,
                      aNow - acEvent.Ms, acEvent.ThreadId);
    else
        spdlog::error("    {} {:#x}, {} ms before the crash, thread {}:", acpWhat, acEvent.Block, aNow - acEvent.Ms, acEvent.ThreadId);
    LogFrames(acEvent);
}

// The recorded life of the memory at aAddress in the ring, oldest first: each allocation that held
// it and the free that ended it, up to the last cLives allocations; or, when it was allocated
// before the ring's window, the last free of a block starting there.
void LogStory(const char* acpWhere, uintptr_t aAddress, uint64_t aFirst, uint64_t aEnd, uint64_t aNow) noexcept
{
    constexpr int cLives = 3;
    uint64_t lives[cLives];
    int count = 0;
    for (uint64_t i = aEnd; i-- > aFirst && count < cLives;)
    {
        const Event& event = s_events[i % s_capacity];
        if (event.Size && aAddress >= event.Block && aAddress - event.Block < event.Size)
            lives[count++] = i;
    }
    if (!count)
    {
        for (uint64_t i = aEnd; i-- > aFirst;)
        {
            const Event& event = s_events[i % s_capacity];
            if (!event.Size && event.Block == aAddress)
            {
                spdlog::error("  {} {:#x}: allocated before the window (watch it from boot: SKYRIMCOOP_HEAP_WATCH)", acpWhere, aAddress);
                LogEvent("FREED", event, aNow);
                return;
            }
        }
        return;
    }

    const Event& newest = s_events[lives[0] % s_capacity];
    spdlog::error("  {} {:#x}: {:#x} into a block of {:#x} bytes", acpWhere, aAddress, aAddress - newest.Block, newest.Size);
    for (int life = count; life-- > 0;)
    {
        const Event& allocation = s_events[lives[life] % s_capacity];
        LogEvent("allocated", allocation, aNow);
        // Its free: the first of its block after it, before the next life.
        const uint64_t cUntil = life ? lives[life - 1] : aEnd;
        bool freed = false;
        for (uint64_t i = lives[life] + 1; i < cUntil; ++i)
        {
            const Event& event = s_events[i % s_capacity];
            if (!event.Size && event.Block == allocation.Block)
            {
                LogEvent("FREED", event, aNow);
                freed = true;
                break;
            }
        }
        if (!freed && !life)
            spdlog::error("    (not freed: still allocated)");
    }
}
} // namespace

namespace HeapHistory
{
bool Start() noexcept
{
    static const bool s_started = []
    {
        // SKYRIMCOOP_HEAP_WATCH=0x215A0DB0,0x21E31E00: addresses a crash showed (they repeat from
        // run to run when the game boots the same way).
        if (const char* pWatch = std::getenv("SKYRIMCOOP_HEAP_WATCH"))
        {
            for (const char* p = pWatch; *p && s_watchCount < cMaxWatches;)
            {
                char* pEnd = nullptr;
                const uintptr_t cAddress = std::strtoull(p, &pEnd, 16);
                if (pEnd == p)
                    break;
                if (cAddress)
                    s_watches[s_watchCount++] = cAddress;
                p = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
            if (s_watchCount)
            {
                s_watched = static_cast<WatchedEvent*>(
                    VirtualAlloc(nullptr, cMaxWatched * sizeof(WatchedEvent), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
                if (!s_watched)
                    s_watchCount = 0;
                else
                    spdlog::warn("Heap history: watching {} addresses from boot", s_watchCount);
            }
        }

        // SKYRIMCOOP_HEAP_QUARANTINE=<MB held back, default 256>.
        if (const char* pQuarantine = std::getenv("SKYRIMCOOP_HEAP_QUARANTINE"); pQuarantine && *pQuarantine &&
                                                                                   std::strcmp(pQuarantine, "0") != 0)
        {
            const int cMegabytes = std::atoi(pQuarantine);
            s_quarantineLimit = static_cast<size_t>(cMegabytes > 1 ? cMegabytes : 256) << 20;
            s_sizeStripes = new SizeStripe[cStripes];
            spdlog::warn("Heap quarantine on: freed blocks are filled with {:#x} and held back from reuse ({} MB)", cPoison,
                         s_quarantineLimit >> 20);
        }

        // SKYRIMCOOP_HEAP_GUARD=0x130,0x40: block sizes to guard (from a heap history report).
        if (const char* pGuard = std::getenv("SKYRIMCOOP_HEAP_GUARD"))
        {
            for (const char* p = pGuard; *p && s_guardSizeCount < cMaxGuardSizes;)
            {
                char* pEnd = nullptr;
                const auto cSize = static_cast<uint32_t>(std::strtoul(p, &pEnd, 16));
                if (pEnd == p)
                    break;
                if (cSize)
                    s_guardSizes[s_guardSizeCount++] = cSize;
                p = *pEnd == ',' ? pEnd + 1 : pEnd;
            }
            if (s_guardSizeCount)
                spdlog::warn("Heap guard on: blocks of {} sizes get pages of their own, inaccessible once freed", s_guardSizeCount);
        }

        const char* pSetting = std::getenv("SKYRIMCOOP_HEAP_HISTORY");
        if (pSetting && *pSetting && std::strcmp(pSetting, "0") != 0)
        {
            const int cMillions = std::atoi(pSetting);
            s_capacity = static_cast<size_t>(cMillions > 1 ? cMillions : 2) * 1000000;
            s_events = static_cast<Event*>(VirtualAlloc(nullptr, s_capacity * sizeof(Event), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (s_events)
                spdlog::warn("Heap history on: the last {} engine allocations and frees with their call stacks ({} MB)", s_capacity,
                             s_capacity * sizeof(Event) >> 20);
            else
                spdlog::error("Heap history: could not reserve {} MB", s_capacity * sizeof(Event) >> 20);
        }
        return s_events || s_watchCount || s_quarantineLimit || s_guardSizeCount;
    }();
    return s_started;
}

void OnAllocate(const void* apBlock, size_t aSize) noexcept
{
    if (s_events || s_watchCount)
        Add(apBlock, static_cast<uint32_t>(aSize ? aSize : 1));
    if (s_quarantineLimit)
    {
        const auto cBlock = reinterpret_cast<uintptr_t>(apBlock);
        SizeStripe& stripe = StripeOf(cBlock);
        std::lock_guard lock(stripe.Lock);
        stripe.Sizes[cBlock] = static_cast<uint32_t>(aSize);
    }
}

void OnFree(const void* apBlock) noexcept
{
    if (s_events || s_watchCount)
        Add(apBlock, 0);
}

bool Quarantine(void* apBlock, bool aAligned) noexcept
{
    if (!s_quarantineLimit)
        return false;
    const auto cBlock = reinterpret_cast<uintptr_t>(apBlock);
    uint32_t size = 0;
    {
        SizeStripe& stripe = StripeOf(cBlock);
        std::lock_guard lock(stripe.Lock);
        const auto cFound = stripe.Sizes.find(cBlock);
        if (cFound == stripe.Sizes.end())
            return false; // allocated before our hooks: its size is unknown
        size = cFound->second;
        stripe.Sizes.erase(cFound);
    }
    std::memset(apBlock, cPoison, size);
    std::lock_guard lock(s_heldLock);
    s_held.push_back({apBlock, size, aAligned});
    s_heldBytes += size;
    return true;
}

void* GuardAllocate(size_t aSize) noexcept
{
    if (!std::count(s_guardSizes, s_guardSizes + s_guardSizeCount, static_cast<uint32_t>(aSize)))
        return nullptr;
    // Page aligned: any alignment the engine asks for.
    void* pBlock = VirtualAlloc(nullptr, (aSize + cPage - 1) & ~(cPage - 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (pBlock)
    {
        std::lock_guard lock(s_guardLock);
        s_guardLive[reinterpret_cast<uintptr_t>(pBlock)] = static_cast<uint32_t>(aSize);
    }
    return pBlock;
}

size_t GuardedSize(const void* apBlock) noexcept
{
    if (!s_guardSizeCount)
        return 0;
    std::lock_guard lock(s_guardLock);
    const auto cFound = s_guardLive.find(reinterpret_cast<uintptr_t>(apBlock));
    return cFound == s_guardLive.end() ? 0 : cFound->second;
}

bool GuardFree(void* apBlock) noexcept
{
    if (!s_guardSizeCount)
        return false;
    uint32_t size = 0;
    {
        std::lock_guard lock(s_guardLock);
        const auto cFound = s_guardLive.find(reinterpret_cast<uintptr_t>(apBlock));
        if (cFound == s_guardLive.end())
            return false;
        size = cFound->second;
        s_guardLive.erase(cFound);
        s_guardFreed[reinterpret_cast<uintptr_t>(apBlock)] = size;
    }
    // Decommitted but still reserved: nothing else gets this address, and touching it faults.
    VirtualFree(apBlock, (size + cPage - 1) & ~(cPage - 1), MEM_DECOMMIT);
    return true;
}

bool TakeEvicted(void*& apBlock, bool& aAligned) noexcept
{
    std::lock_guard lock(s_heldLock);
    if (s_heldBytes <= s_quarantineLimit || s_held.empty())
        return false;
    const Held cOldest = s_held.front();
    s_held.pop_front();
    s_heldBytes -= cOldest.Size;
    apBlock = cOldest.Block;
    aAligned = cOldest.Aligned;
    return true;
}

void Dump(const _CONTEXT& acContext, uintptr_t aFaultAddress) noexcept
{
    if (!s_events && !s_watchCount && !s_quarantineLimit && !s_guardSizeCount)
        return;
    const uint64_t cNow = GetTickCount64();

    if (s_guardSizeCount)
    {
        // No lock: the faulting thread may hold it; the maps are only read.
        const uintptr_t cPageOf = aFaultAddress & ~(cPage - 1);
        const auto cFound = s_guardFreed.find(cPageOf);
        if (cFound != s_guardFreed.end())
            spdlog::error("Heap guard: the crash is an access to {:#x}, {:#x} into a FREED block of {:#x} bytes at {:#x}: a "
                          "use after free, at the instruction that made it (who freed it: the history below)",
                          aFaultAddress, aFaultAddress - cPageOf, cFound->second, cPageOf);
        else
            spdlog::error("Heap guard: the crash's access ({:#x}) is not in a freed guarded block", aFaultAddress);
    }

    if (s_quarantineLimit)
    {
        constexpr uint64_t cPoisoned = 0x0101010101010101ull * cPoison;
        const struct
        {
            const char* Name;
            DWORD64 Value;
        } cValues[] = {{"RAX", acContext.Rax}, {"RBX", acContext.Rbx}, {"RCX", acContext.Rcx}, {"RDX", acContext.Rdx},
                       {"RSI", acContext.Rsi}, {"RDI", acContext.Rdi}, {"RBP", acContext.Rbp}, {"R8", acContext.R8},
                       {"R9", acContext.R9},   {"R10", acContext.R10}, {"R11", acContext.R11}, {"R12", acContext.R12},
                       {"R13", acContext.R13}, {"R14", acContext.R14}, {"R15", acContext.R15}};
        for (const auto& value : cValues)
            if ((value.Value & ~0xFFFFull) == (cPoisoned & ~0xFFFFull))
                spdlog::error("Heap quarantine: {} = {:#x} was read from FREED memory (the poison the quarantine fills it "
                              "with): the pointer it came from is a use after free",
                              value.Name, value.Value);
    }

    if (s_watchCount)
    {
        const size_t cCount = std::min(s_watchedNext.load(), cMaxWatched);
        spdlog::error("Heap history of the watched addresses, since boot ({} events{}):", cCount,
                      cCount == cMaxWatched ? ", the first ones only" : "");
        for (size_t watch = 0; watch < s_watchCount; ++watch)
        {
            spdlog::error("  watched {:#x}:", s_watches[watch]);
            bool any = false;
            for (size_t i = 0; i < cCount; ++i)
            {
                if (s_watched[i].Watch != watch)
                    continue;
                const Event& event = s_watched[i].Event;
                if (event.Size)
                    spdlog::error("    ({:#x} into the block)", s_watches[watch] - event.Block);
                LogEvent(event.Size ? "allocated" : "FREED", event, cNow);
                any = true;
            }
            if (!any)
                spdlog::error("    (no allocation or free since boot: not the engine heap's, or allocated before our hooks)");
        }
    }

    if (!s_events)
    {
        spdlog::error("End of heap history");
        return;
    }
    const uint64_t cEnd = s_next.load();
    const uint64_t cFirst = cEnd > s_capacity ? cEnd - s_capacity : 0;
    spdlog::error("Heap history of the memory the crash's registers and stack point into ({} events back, {} ms):",
                  cEnd - cFirst, cEnd > cFirst ? cNow - s_events[cFirst % s_capacity].Ms : 0);

    // Each address once, under the first place that holds it.
    uintptr_t seen[64];
    size_t seenCount = 0;
    const auto story = [&](const char* acpWhere, uintptr_t aAddress)
    {
        if (aAddress <= 0x10000)
            return;
        for (size_t i = 0; i < seenCount; ++i)
            if (seen[i] == aAddress)
                return;
        if (seenCount < std::size(seen))
            seen[seenCount++] = aAddress;
        LogStory(acpWhere, aAddress, cFirst, cEnd, cNow);
    };

    const struct
    {
        const char* Name;
        DWORD64 Value;
    } cRegisters[] = {{"RAX", acContext.Rax}, {"RBX", acContext.Rbx}, {"RCX", acContext.Rcx}, {"RDX", acContext.Rdx},
                      {"RSI", acContext.Rsi}, {"RDI", acContext.Rdi}, {"RBP", acContext.Rbp}, {"R8", acContext.R8},
                      {"R9", acContext.R9},   {"R10", acContext.R10}, {"R11", acContext.R11}, {"R12", acContext.R12},
                      {"R13", acContext.R13}, {"R14", acContext.R14}, {"R15", acContext.R15}};
    for (const auto& reg : cRegisters)
        story(reg.Name, reg.Value);

    // The stack's values: the caller's locals and saved registers (the object it was walking).
    for (int slot = 0; slot < 32; ++slot)
    {
        const auto* pSlot = reinterpret_cast<const DWORD64*>(acContext.Rsp) + slot;
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(pSlot, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT)
            break;
        char where[16];
        snprintf(where, sizeof(where), "[RSP+%X]", slot * 8);
        story(where, *pSlot);
    }
    spdlog::error("End of heap history");
}
} // namespace HeapHistory
