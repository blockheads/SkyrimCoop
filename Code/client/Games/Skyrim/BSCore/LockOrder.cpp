#include <TiltedOnlinePCH.h>

#include "LockOrder.h"

#include <HotReload/Handoff.h>
#include <SkseBranchPatch.hpp>

#include <charconv>
#include <intrin.h>
#include <mutex>
#include <span>

// BSReadWriteLock (1.6.1170): {uint32 writer thread id, uint32 lock word}. The word holds the
// reader count, or 0x80000001 plus nested acquisitions while a thread writes. The writer may
// lock again in either mode. A write waits for the word to reach 0 and readers never wait for
// a waiting writer, so read-read never deadlocks; a reader asking to write deadlocks itself.

namespace
{
constexpr uint8_t cRead = 0;
constexpr uint8_t cWrite = 1;

bool Conflicts(uint8_t aFirst, uint8_t aSecond)
{
    return aFirst == cWrite || aSecond == cWrite;
}

const char* ModeName(uint8_t aMode)
{
    return aMode == cWrite ? "write" : "read";
}

struct Held
{
    const void* Lock;
    uint64_t Caller;
    uint32_t SinceMs;
    uint8_t Mode;
    bool Acquired;
};

// One per thread that ever took a lock; written only by that thread, peeked by the watchdog.
struct ThreadSlot
{
    std::atomic<uint32_t> ThreadId;
    std::atomic<uint32_t> Depth; // can exceed the stored entries
    Held Entries[24];
};

ThreadSlot s_threads[512];
std::atomic<uint32_t> s_threadCount{0};
thread_local ThreadSlot* t_slot = nullptr;
thread_local bool t_reporting = false;

// Observed "Outer held, then Inner taken" pairs, keyed by an ordered hash of the two locks.
struct Edge
{
    std::atomic<uint64_t> Key;
    std::atomic<uint8_t> Modes; // bit (outer mode * 2 + inner mode)
    std::atomic<bool> Reported;
    std::atomic<bool> ManyThreads; // seen on more than the first thread
    uint32_t ThreadId;
    uint64_t OuterCaller;
    uint64_t InnerCaller;
};

constexpr size_t cEdgeCapacity = 1 << 20;
// 32MB, allocated once per game: a hot reload hands it to the next client (the old client's
// image is never freed, so a static table would stay behind with it on every reload).
Edge* s_edges = nullptr;
constexpr char cEdgesHandoffKey[] = "lock_order_edges";
std::atomic<uint32_t> s_edgeCount{0};
std::atomic<uint32_t> s_tooDeep{0};
std::atomic<uint32_t> s_tableFull{0};
std::atomic<uint32_t> s_tableResets{0};
std::atomic<bool> s_clearing{false};

// Report each pattern (the call sites involved) once, not once per pair of objects.
std::atomic<uint64_t> s_reportedPatterns[1024];
std::atomic<uint32_t> s_reportCount{0};

bool s_enabled = false;
uintptr_t s_engineBase = 0;
uintptr_t s_engineEnd = 0;
uintptr_t s_ourBase = 0;
uintptr_t s_ourEnd = 0;
std::atomic<LockOrder::TDescribeLock> s_describe{nullptr};

std::mutex s_reportLock;
std::vector<std::string> s_reports;

using TLockFunction = void(void*);
TLockFunction* RealLockForRead = nullptr;
TLockFunction* RealLockForWrite = nullptr;
TLockFunction* RealUnlockForRead = nullptr;
TLockFunction* RealUnlockForWrite = nullptr;

uint64_t EdgeKey(const void* apOuter, const void* apInner)
{
    uint64_t hash = reinterpret_cast<uint64_t>(apOuter) * 0x9E3779B97F4A7C15ull;
    hash ^= _rotl64(reinterpret_cast<uint64_t>(apInner), 31) * 0xC2B2AE3D27D4EB4Full;
    hash ^= hash >> 29;
    return hash | 1;
}

Edge* FindEdge(const void* apOuter, const void* apInner)
{
    const uint64_t cKey = EdgeKey(apOuter, apInner);
    for (size_t probe = 0; probe < 64; ++probe)
    {
        Edge& edge = s_edges[(cKey + probe) & (cEdgeCapacity - 1)];
        const uint64_t cSeen = edge.Key.load(std::memory_order_acquire);
        if (cSeen == cKey)
            return &edge;
        if (cSeen == 0)
            return nullptr;
    }
    return nullptr;
}

// Freed objects leave their pairs behind; when the table fills up, start over (racy but
// harmless: concurrent lookups miss an edge or two).
void ResetEdges()
{
    bool expected = false;
    if (!s_clearing.compare_exchange_strong(expected, true))
        return;
    for (Edge& edge : std::span(s_edges, cEdgeCapacity))
    {
        edge.Key.store(0, std::memory_order_relaxed);
        edge.Modes.store(0, std::memory_order_relaxed);
        edge.Reported.store(false, std::memory_order_relaxed);
        edge.ManyThreads.store(false, std::memory_order_relaxed);
    }
    s_edgeCount.store(0, std::memory_order_relaxed);
    s_tableResets.fetch_add(1, std::memory_order_relaxed);
    s_clearing.store(false, std::memory_order_release);
}

bool FirstOfPattern(uint64_t aPattern)
{
    aPattern |= 1;
    for (size_t probe = 0; probe < 32; ++probe)
    {
        auto& slot = s_reportedPatterns[(aPattern + probe) & (std::size(s_reportedPatterns) - 1)];
        uint64_t seen = slot.load(std::memory_order_acquire);
        if (seen == aPattern)
            return false;
        if (seen == 0 && slot.compare_exchange_strong(seen, aPattern))
            return true;
        if (seen == aPattern)
            return false;
    }
    return false;
}

void RecordEdge(const Held& acOuter, const void* apInner, uint8_t aInnerMode, uint64_t aInnerCaller, uint32_t aThreadId)
{
    if (s_clearing.load(std::memory_order_relaxed))
        return;
    if (s_edgeCount.load(std::memory_order_relaxed) > cEdgeCapacity / 2)
        ResetEdges();
    const uint64_t cKey = EdgeKey(acOuter.Lock, apInner);
    const auto cBit = static_cast<uint8_t>(1u << (acOuter.Mode * 2 + aInnerMode));
    for (size_t probe = 0; probe < 64; ++probe)
    {
        Edge& edge = s_edges[(cKey + probe) & (cEdgeCapacity - 1)];
        uint64_t seen = edge.Key.load(std::memory_order_acquire);
        if (seen == 0)
        {
            if (edge.Key.compare_exchange_strong(seen, cKey, std::memory_order_acq_rel))
            {
                edge.ThreadId = aThreadId;
                edge.OuterCaller = acOuter.Caller;
                edge.InnerCaller = aInnerCaller;
                edge.Modes.fetch_or(cBit, std::memory_order_release);
                s_edgeCount.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        if (seen == cKey)
        {
            if ((edge.Modes.load(std::memory_order_relaxed) & cBit) == 0)
                edge.Modes.fetch_or(cBit, std::memory_order_release);
            if (edge.ThreadId != aThreadId && !edge.ManyThreads.load(std::memory_order_relaxed))
                edge.ManyThreads.store(true, std::memory_order_relaxed);
            return;
        }
    }
    s_tableFull.fetch_add(1, std::memory_order_relaxed);
    ResetEdges();
}

// "SkyrimSE.exe+0x1234" without the loader lock (the reporting thread may hold engine locks).
std::string FormatAddress(uint64_t aAddress)
{
    if (aAddress >= s_engineBase && aAddress < s_engineEnd)
        return fmt::format("SkyrimSE.exe+{:#x}", aAddress - s_engineBase);
    if (aAddress >= s_ourBase && aAddress < s_ourEnd)
        return fmt::format("SkyrimTogetherClient.dll+{:#x}", aAddress - s_ourBase);
    return fmt::format("{:#x}", aAddress);
}

std::string DescribeLock(const void* apLock)
{
    char name[96];
    if (auto* pDescribe = s_describe.load(std::memory_order_acquire))
        if (pDescribe(apLock, name, sizeof(name)))
            return fmt::format("{} ({})", fmt::ptr(apLock), name);
    return fmt::format("{}", fmt::ptr(apLock));
}

// aPattern identifies the call sites involved; each pattern is reported once. Reports with our
// DLL on the reporting thread's stack are tagged "[ours]".
void Report(uint64_t aPattern, std::string aMessage)
{
    if (!FirstOfPattern(aPattern))
        return;
    t_reporting = true;
    const uint32_t cIndex = s_reportCount.fetch_add(1, std::memory_order_relaxed);
    if (cIndex < 64)
    {
        void* frames[32];
        const USHORT cCount = RtlCaptureStackBackTrace(1, 32, frames, nullptr);
        std::string stack;
        // Our frames above the engine's lock call are this checker itself.
        bool belowEngine = false;
        bool ours = false;
        for (USHORT i = 0; i < cCount; ++i)
        {
            const auto cFrame = reinterpret_cast<uint64_t>(frames[i]);
            const bool cOurFrame = cFrame >= s_ourBase && cFrame < s_ourEnd;
            belowEngine |= !cOurFrame;
            ours |= belowEngine && cOurFrame;
            stack += fmt::format("\n  backtrace #{:02} {}", i, FormatAddress(cFrame));
        }
        if (ours)
            aMessage = "[ours] " + aMessage;
        spdlog::error("LockOrder: {}{}", aMessage, stack);
        spdlog::default_logger()->flush();

        std::scoped_lock _(s_reportLock);
        s_reports.push_back(std::move(aMessage));
    }
    t_reporting = false;
}

ThreadSlot* CurrentSlot(uint32_t aThreadId)
{
    if (t_slot)
        return t_slot;
    const uint32_t cIndex = s_threadCount.fetch_add(1, std::memory_order_acq_rel);
    if (cIndex >= std::size(s_threads))
    {
        s_threadCount.store(static_cast<uint32_t>(std::size(s_threads)), std::memory_order_relaxed);
        return nullptr;
    }
    t_slot = &s_threads[cIndex];
    t_slot->ThreadId.store(aThreadId, std::memory_order_release);
    return t_slot;
}

// Checks and records the acquisition before the engine starts waiting: a hang then shows who
// waits for what. Returns the entry to mark acquired, or -1.
int BeginAcquire(const void* apLock, uint8_t aMode, uint64_t aCaller)
{
    const uint32_t cThreadId = GetCurrentThreadId();
    ThreadSlot* pSlot = CurrentSlot(cThreadId);
    if (!pSlot)
        return -1;

    const uint32_t cDepth = pSlot->Depth.load(std::memory_order_relaxed);
    const uint32_t cStored = std::min<uint32_t>(cDepth, static_cast<uint32_t>(std::size(pSlot->Entries)));
    const bool cRecursive = *static_cast<const volatile uint32_t*>(apLock) == cThreadId;

    if (!cRecursive)
    {
        for (uint32_t i = 0; i < cStored; ++i)
        {
            const Held& cHeld = pSlot->Entries[i];
            if (cHeld.Lock == apLock)
            {
                if (aMode == cWrite && cHeld.Mode == cRead)
                    Report(EdgeKey(reinterpret_cast<const void*>(cHeld.Caller), reinterpret_cast<const void*>(aCaller)),
                           fmt::format("self-deadlock [{} > {}]: thread {} asks for the write lock on {} while it "
                                       "holds the read lock",
                                       FormatAddress(cHeld.Caller), FormatAddress(aCaller), cThreadId,
                                       DescribeLock(apLock)));
                continue;
            }

            RecordEdge(cHeld, apLock, aMode, aCaller, cThreadId);

            // Another thread took this lock first, then the one we hold: does each block the other?
            // One thread alone cannot deadlock on order (the loader takes extra-data locks in both
            // orders): the reverse order must come from another thread.
            Edge* pReverse = FindEdge(apLock, cHeld.Lock);
            if (!pReverse || (pReverse->ThreadId == cThreadId && !pReverse->ManyThreads.load(std::memory_order_relaxed)))
                continue;
            const uint8_t cModes = pReverse->Modes.load(std::memory_order_acquire);
            for (uint8_t theirHeld = 0; theirHeld < 2; ++theirHeld)
                for (uint8_t theirWant = 0; theirWant < 2; ++theirWant)
                {
                    if ((cModes & (1u << (theirHeld * 2 + theirWant))) == 0)
                        continue;
                    if (!Conflicts(theirWant, cHeld.Mode) || !Conflicts(aMode, theirHeld))
                        continue;
                    if (pReverse->Reported.exchange(true))
                        continue;
                    const uint64_t cPattern =
                        EdgeKey(reinterpret_cast<const void*>(cHeld.Caller), reinterpret_cast<const void*>(aCaller)) ^
                        _rotl64(EdgeKey(reinterpret_cast<const void*>(pReverse->OuterCaller),
                                        reinterpret_cast<const void*>(pReverse->InnerCaller)), 17);
                    Report(cPattern, fmt::format(
                        "lock order inversion [{} > {} vs {} > {}]: thread {} holds {} for {} (taken by {}) and asks "
                        "for {} for {} (by {}); thread {} held {} for {} (taken by {}) and asked for {} for {} (by {})",
                        FormatAddress(cHeld.Caller), FormatAddress(aCaller), FormatAddress(pReverse->OuterCaller),
                        FormatAddress(pReverse->InnerCaller), cThreadId, DescribeLock(cHeld.Lock), ModeName(cHeld.Mode),
                        FormatAddress(cHeld.Caller), DescribeLock(apLock), ModeName(aMode), FormatAddress(aCaller),
                        pReverse->ThreadId, DescribeLock(apLock),
                        ModeName(theirHeld), FormatAddress(pReverse->OuterCaller), DescribeLock(cHeld.Lock),
                        ModeName(theirWant), FormatAddress(pReverse->InnerCaller)));
                }
        }
    }

    pSlot->Depth.store(cDepth + 1, std::memory_order_release);
    if (cDepth >= std::size(pSlot->Entries))
    {
        s_tooDeep.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    pSlot->Entries[cDepth] = {apLock, aCaller, GetTickCount(), aMode, false};
    return static_cast<int>(cDepth);
}

void EndAcquire(int aEntry)
{
    if (aEntry >= 0 && t_slot)
        t_slot->Entries[aEntry].Acquired = true;
}

void Release(const void* apLock, uint8_t aMode)
{
    ThreadSlot* pSlot = t_slot;
    if (!pSlot)
        return;
    const uint32_t cDepth = pSlot->Depth.load(std::memory_order_relaxed);
    if (cDepth == 0)
        return; // Taken by code that inlines the lock: not tracked.
    const uint32_t cStored = std::min<uint32_t>(cDepth, static_cast<uint32_t>(std::size(pSlot->Entries)));

    // The newest entry for this lock, preferring the same mode.
    int found = -1;
    for (int i = static_cast<int>(cStored) - 1; i >= 0; --i)
        if (pSlot->Entries[i].Lock == apLock)
        {
            if (found < 0)
                found = i;
            if (pSlot->Entries[i].Mode == aMode)
            {
                found = i;
                break;
            }
        }

    if (found < 0)
    {
        if (cDepth > cStored)
            pSlot->Depth.store(cDepth - 1, std::memory_order_release); // An untracked deep entry.
        return;
    }
    for (uint32_t i = static_cast<uint32_t>(found); i + 1 < cStored; ++i)
        pSlot->Entries[i] = pSlot->Entries[i + 1];
    pSlot->Depth.store(cDepth - 1, std::memory_order_release);
}

void HookLockForRead(void* apLock)
{
    if (t_reporting)
        return RealLockForRead(apLock);
    const int cEntry = BeginAcquire(apLock, cRead, reinterpret_cast<uint64_t>(_ReturnAddress()));
    RealLockForRead(apLock);
    EndAcquire(cEntry);
}

void HookLockForWrite(void* apLock)
{
    if (t_reporting)
        return RealLockForWrite(apLock);
    const int cEntry = BeginAcquire(apLock, cWrite, reinterpret_cast<uint64_t>(_ReturnAddress()));
    RealLockForWrite(apLock);
    EndAcquire(cEntry);
}

void HookUnlockForRead(void* apLock)
{
    Release(apLock, cRead);
    RealUnlockForRead(apLock);
}

void HookUnlockForWrite(void* apLock)
{
    Release(apLock, cWrite);
    RealUnlockForWrite(apLock);
}

void ModuleRange(HMODULE aModule, uintptr_t& aBase, uintptr_t& aEnd)
{
    aBase = reinterpret_cast<uintptr_t>(aModule);
    const auto* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(aModule);
    const auto* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(aBase + pDos->e_lfanew);
    aEnd = aBase + pNt->OptionalHeader.SizeOfImage;
}
} // namespace

extern "C" IMAGE_DOS_HEADER __ImageBase;

static TiltedPhoques::Initializer s_lockOrderHooks([]() {
    char value[8]{};
    if (GetEnvironmentVariableA("SKYRIMCOOP_LOCKORDER", value, sizeof(value)) > 0 && value[0] == '0')
        return;

    if (const auto cHandoff = CoopReload::TakeHandoff(cEdgesHandoffKey))
    {
        uintptr_t address = 0;
        std::from_chars(cHandoff->data(), cHandoff->data() + cHandoff->size(), address);
        s_edges = reinterpret_cast<Edge*>(address);
    }
    if (s_edges)
        ResetEdges(); // The previous client's pairs name its call sites.
    else
        s_edges = static_cast<Edge*>(VirtualAlloc(nullptr, sizeof(Edge) * cEdgeCapacity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!s_edges)
        return;
    TiltedPhoques::AtUnload([] {
        CoopReload::HandOff(cEdgesHandoffKey, std::to_string(reinterpret_cast<uintptr_t>(s_edges)));
    });

    ModuleRange(GetModuleHandleW(nullptr), s_engineBase, s_engineEnd);
    ModuleRange(reinterpret_cast<HMODULE>(&__ImageBase), s_ourBase, s_ourEnd);

    POINTER_SKYRIMSE(TLockFunction, lockForRead, 68233);
    POINTER_SKYRIMSE(TLockFunction, lockForWrite, 68234);
    POINTER_SKYRIMSE(TLockFunction, unlockForRead, 68239);
    POINTER_SKYRIMSE(TLockFunction, unlockForWrite, 68240);

    RealLockForRead = lockForRead.Get();
    RealLockForWrite = lockForWrite.Get();
    RealUnlockForRead = unlockForRead.Get();
    RealUnlockForWrite = unlockForWrite.Get();

    TP_HOOK(&RealLockForRead, HookLockForRead);
    TP_HOOK(&RealLockForWrite, HookLockForWrite);
    TP_HOOK(&RealUnlockForRead, HookUnlockForRead);
    TP_HOOK(&RealUnlockForWrite, HookUnlockForWrite);
    s_enabled = true;
    spdlog::info("LockOrder: tracking BSReadWriteLock at SkyrimSE.exe+{:#x}",
                 reinterpret_cast<uintptr_t>(RealLockForRead) - s_engineBase);
});

namespace LockOrder
{
void SetLockDescriber(TDescribeLock apDescribe) noexcept
{
    s_describe.store(apDescribe, std::memory_order_release);
}

void LogHeldLocks() noexcept
{
    if (!s_enabled)
        return;
    const uint32_t cNow = GetTickCount();
    const uint32_t cThreads = std::min<uint32_t>(s_threadCount.load(), static_cast<uint32_t>(std::size(s_threads)));
    for (uint32_t t = 0; t < cThreads; ++t)
    {
        const ThreadSlot& cSlot = s_threads[t];
        const uint32_t cDepth = cSlot.Depth.load(std::memory_order_acquire);
        if (cDepth == 0)
            continue;
        spdlog::error("LockOrder: thread {} has {} lock(s):", cSlot.ThreadId.load(), cDepth);
        const uint32_t cStored = std::min<uint32_t>(cDepth, static_cast<uint32_t>(std::size(cSlot.Entries)));
        for (uint32_t i = 0; i < cStored; ++i)
        {
            const Held cHeld = cSlot.Entries[i];
            const auto* pWords = static_cast<const volatile uint32_t*>(cHeld.Lock);
            spdlog::error("  {} {} {} for {}ms, taken by {} (lock: writer thread {}, word {:#x})",
                          cHeld.Acquired ? "holds" : "WAITS FOR", ModeName(cHeld.Mode), DescribeLock(cHeld.Lock),
                          cNow - cHeld.SinceMs, FormatAddress(cHeld.Caller), pWords[0], pWords[1]);
        }
    }
    spdlog::error("LockOrder: (end of held locks; {} edges, {} reports)", s_edgeCount.load(), s_reportCount.load());
}

Stats GetStats() noexcept
{
    return {s_enabled, s_edgeCount.load(), s_reportCount.load(), s_tooDeep.load(), s_tableFull.load(), s_tableResets.load()};
}

std::vector<std::string> GetReports() noexcept
{
    std::scoped_lock _(s_reportLock);
    return s_reports;
}
} // namespace LockOrder
