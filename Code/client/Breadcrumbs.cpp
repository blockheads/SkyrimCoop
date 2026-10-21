#include "Breadcrumbs.h"
#include <Games/Rtti.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace
{
struct Entry
{
    const char* What;
    const void* Object; // a hook's first argument (0 for bridge calls)
    char Subject[48];   // what Object was when first recorded: "Actor FF00081E", "NiNode", ""
    uint32_t FormId;
    uint32_t Detail;
    uint32_t ThreadId;
    uint32_t Count;
    uint64_t FirstMs;
    uint64_t LastMs;
};

// Each hook (and each kind of bridge call) keeps its own recent history, so a hook that runs on
// thousands of objects in a cell load can only push out its own entries, never another's.
constexpr size_t cPerChannel = 24;
constexpr size_t cChannels = 512; // hooks and bridge calls together are well under this
constexpr uint64_t cDumpedMs = 10000; // what ran in the last 10s

struct Channel
{
    const char* What; // a string literal: the same pointer every call
    uint64_t Total;
    uint64_t LastMs;
    uint32_t Next;
    std::array<Entry, cPerChannel> Recent;
};
std::array<Channel, cChannels> s_channels{};

// The channel of acpWhat, made on first use (open addressing on the literal's address).
Channel* ChannelOf(const char* acpWhat) noexcept
{
    size_t slot = (reinterpret_cast<uintptr_t>(acpWhat) >> 3) % cChannels;
    for (size_t probe = 0; probe < cChannels; ++probe, slot = (slot + 1) % cChannels)
    {
        Channel& channel = s_channels[slot];
        if (channel.What == acpWhat)
            return &channel;
        if (!channel.What)
        {
            channel.What = acpWhat;
            return &channel;
        }
    }
    return nullptr;
}

std::atomic_flag s_lock = ATOMIC_FLAG_INIT;

// The object a hook ran on: "Actor FF00081E" for a form, "NiNode" otherwise, "" when it is not (yet) an object of the
// game's: a hook's first argument may be a number, or an object still being constructed.
void Describe(const void* apObject, char* apOut, size_t aSize) noexcept
{
    if (!Rtti::ClassName(apObject, apOut, aSize - 12) || !Rtti::IsForm(apObject))
        return;
    const size_t cLength = strlen(apOut);
    snprintf(apOut + cLength, aSize - cLength, " %08X", *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(apObject) + 0x14));
}

void Add(const char* acpWhat, const void* apObject, uint32_t aFormId, uint32_t aDetail) noexcept
{
    const uint64_t cNow = GetTickCount64();
    const uint32_t cThread = GetCurrentThreadId();
    while (s_lock.test_and_set(std::memory_order_acquire))
        YieldProcessor();
    Channel* pChannel = ChannelOf(acpWhat);
    if (!pChannel)
    {
        s_lock.clear(std::memory_order_release);
        return;
    }
    ++pChannel->Total;
    pChannel->LastMs = cNow;
    // A repeat (the same object, form and detail: per-frame calls) counts up its entry.
    for (Entry& recent : pChannel->Recent)
    {
        if (recent.Count && recent.Object == apObject && recent.FormId == aFormId && recent.Detail == aDetail)
        {
            ++recent.Count;
            recent.LastMs = cNow;
            recent.ThreadId = cThread;
            s_lock.clear(std::memory_order_release);
            return;
        }
    }
    // New: what the object is now (it may be freed by the time of a crash). Read outside the lock.
    s_lock.clear(std::memory_order_release);
    Entry entry{acpWhat, apObject, "", aFormId, aDetail, cThread, 1, cNow, cNow};
    if (apObject)
        Describe(apObject, entry.Subject, sizeof(entry.Subject));
    while (s_lock.test_and_set(std::memory_order_acquire))
        YieldProcessor();
    pChannel->Recent[pChannel->Next++ % cPerChannel] = entry;
    s_lock.clear(std::memory_order_release);
}
} // namespace

namespace Breadcrumbs
{
void Record(const char* acpWhat, uint32_t aFormId, uint32_t aDetail) noexcept
{
    Add(acpWhat, nullptr, aFormId, aDetail);
}

void RecordHook(const char* acpHookName, const void* apFirstArgument) noexcept
{
    Add(acpHookName, apFirstArgument, 0, 0);
}

bool ObservesHook(const char* acpHookName) noexcept
{
    // Called thousands of times a frame: recording them would cost frames and flood the ring.
    static constexpr const char* cTooFrequent[] = {"HookLockForRead", "HookLockForWrite", "HookUnlockForRead",
                                                   "HookUnlockForWrite", "HookFormAllocate", "HookFormFree", "HookFormReallocate"};
    for (const char* pName : cTooFrequent)
        if (strcmp(pName, acpHookName) == 0)
            return false;
    return true;
}

void Dump() noexcept
{
    // Another thread may be mid-record: wait briefly, then read anyway (a torn entry beats none).
    bool locked = false;
    for (int attempt = 0; attempt < 1000 && !(locked = !s_lock.test_and_set(std::memory_order_acquire)); ++attempt)
        YieldProcessor();

    const uint64_t cNow = GetTickCount64();
    spdlog::error("Hooks of ours and engine changes by our code: calls in total, ms since the last (what ran in the last {}s):",
                  cDumpedMs / 1000);
    for (const Channel& channel : s_channels)
        if (channel.What && cNow - channel.LastMs <= cDumpedMs)
            spdlog::error("  total {} {} last {}", channel.What, channel.Total, cNow - channel.LastMs);

    // Every channel's recent entries in the window, oldest first.
    static std::array<const Entry*, cChannels * cPerChannel> s_sorted;
    size_t count = 0;
    for (const Channel& channel : s_channels)
        for (const Entry& entry : channel.Recent)
            if (entry.Count && cNow - entry.LastMs <= cDumpedMs)
                s_sorted[count++] = &entry;
    std::sort(s_sorted.begin(), s_sorted.begin() + count, [](const Entry* a, const Entry* b) { return a->FirstMs < b->FirstMs; });

    spdlog::error("Last engine changes by our code and hooks that ran (in order of first call; ms before the crash: "
                  "first .. last, [last thread], what, form or object, detail, calls; each hook's last {}):", cPerChannel);
    for (size_t i = 0; i < count; ++i)
    {
        const Entry& entry = *s_sorted[i];
        char subject[96];
        if (entry.Object)
            snprintf(subject, sizeof(subject), "%s%s%p", entry.Subject, entry.Subject[0] ? " at " : "", entry.Object);
        else
            snprintf(subject, sizeof(subject), "%08X %X", entry.FormId, entry.Detail);
        if (entry.Count > 1)
            spdlog::error("  {:>7} .. {:>7}  [{}] {} {} x{}", cNow - entry.FirstMs, cNow - entry.LastMs, entry.ThreadId, entry.What, subject,
                          entry.Count);
        else
            spdlog::error("  {:>7}             [{}] {} {}", cNow - entry.FirstMs, entry.ThreadId, entry.What, subject);
    }
    if (locked)
        s_lock.clear(std::memory_order_release);
}
} // namespace Breadcrumbs
