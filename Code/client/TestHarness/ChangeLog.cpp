#include "ChangeLog.h"
#include "Commands.h"

#include <Forms/FormChanges.h>
#include <Forms/TESForm.h>

#include <array>
#include <atomic>
#include <chrono>
#include <Hooks/Initializer.h>

namespace TestHarness::ChangeLog
{
namespace
{
// One (form type, flags, forced) key per slot, claimed once and kept across resets; resets zero the counts.
constexpr size_t cBuckets = 4096;
constexpr size_t cSamples = 4;
// Distinct form ids seen this window (open addressing; ids are never 0).
constexpr size_t cForms = 1 << 16;

struct Bucket
{
    std::atomic<uint64_t> Key{0}; // key + 1, 0 = free
    std::atomic<uint32_t> Calls{0};
    std::atomic<uint32_t> Recorded{0};
    std::array<std::atomic<uint32_t>, cSamples> Samples{};
};

std::array<Bucket, cBuckets> s_buckets;
std::array<std::atomic<uint32_t>, cForms> s_forms;
std::array<std::atomic<uint32_t>, 256> s_distinct;
std::atomic<bool> s_full{false};
std::atomic<int64_t> s_windowStart{0};

int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint64_t Mix(uint64_t aValue)
{
    aValue ^= aValue >> 33;
    aValue *= 0xff51afd7ed558ccdULL;
    return aValue ^ (aValue >> 33);
}

Bucket* FindBucket(uint64_t aKey)
{
    const uint64_t cStored = aKey + 1;
    for (size_t i = 0, slot = Mix(aKey) % cBuckets; i < cBuckets; ++i, slot = (slot + 1) % cBuckets)
    {
        auto& bucket = s_buckets[slot];
        uint64_t current = bucket.Key.load(std::memory_order_acquire);
        if (current == 0 && bucket.Key.compare_exchange_strong(current, cStored, std::memory_order_acq_rel))
            return &bucket;
        if (current == cStored)
            return &bucket;
    }
    s_full.store(true, std::memory_order_relaxed);
    return nullptr;
}

// True the first time this window sees the form.
bool FirstSight(uint32_t aFormId)
{
    for (size_t i = 0, slot = Mix(aFormId) % cForms; i < 64; ++i, slot = (slot + 1) % cForms)
    {
        uint32_t current = s_forms[slot].load(std::memory_order_acquire);
        if (current == aFormId)
            return false;
        if (current == 0 && s_forms[slot].compare_exchange_strong(current, aFormId, std::memory_order_acq_rel))
            return true;
        if (current == aFormId)
            return false;
    }
    s_full.store(true, std::memory_order_relaxed);
    return false;
}

void Count(const TESForm& acForm, uint32_t aFlags, bool aForced, bool aRecorded) noexcept
{
    const auto cType = static_cast<uint8_t>(acForm.formType);
    const uint64_t cKey = uint64_t(cType) | uint64_t(aFlags) << 8 | uint64_t(aForced) << 40;
    if (auto* pBucket = FindBucket(cKey))
    {
        pBucket->Calls.fetch_add(1, std::memory_order_relaxed);
        if (aRecorded)
            pBucket->Recorded.fetch_add(1, std::memory_order_relaxed);
        for (auto& sample : pBucket->Samples)
        {
            uint32_t current = sample.load(std::memory_order_relaxed);
            if (current == acForm.formID)
                break;
            if (current == 0 && sample.compare_exchange_strong(current, acForm.formID, std::memory_order_relaxed))
                break;
        }
    }
    if (aRecorded && acForm.formID && FirstSight(acForm.formID))
        s_distinct[cType].fetch_add(1, std::memory_order_relaxed);
}

Hooks::Initializer s_changeLogWatcher(
    []()
    {
        FormChanges::Watch(&Count);
        s_windowStart.store(NowMs());
    });

nlohmann::json ChangeLogCommand(Harness&, const nlohmann::json&)
{
    auto response = Read();
    response["ok"] = true;
    return response;
}

nlohmann::json ChangeLogResetCommand(Harness&, const nlohmann::json&)
{
    Reset();
    return Ok();
}

constexpr Command cCommands[] = {
    {"change_log", false, &ChangeLogCommand},
    {"change_log_reset", false, &ChangeLogResetCommand},
};
} // namespace

nlohmann::json Read()
{
    auto buckets = nlohmann::json::array();
    for (const auto& cBucket : s_buckets)
    {
        const uint64_t cStored = cBucket.Key.load(std::memory_order_acquire);
        const uint32_t cCalls = cBucket.Calls.load(std::memory_order_relaxed);
        if (cStored == 0 || cCalls == 0)
            continue;
        const uint64_t cKey = cStored - 1;
        auto samples = nlohmann::json::array();
        for (const auto& cSample : cBucket.Samples)
            if (const uint32_t cId = cSample.load(std::memory_order_relaxed))
                samples.push_back(cId);
        buckets.push_back({{"type", cKey & 0xFF},
                           {"flags", (cKey >> 8) & 0xFFFFFFFF},
                           {"forced", (cKey >> 40) != 0},
                           {"calls", cCalls},
                           {"recorded", cBucket.Recorded.load(std::memory_order_relaxed)},
                           {"samples", samples}});
    }
    auto forms = nlohmann::json::object();
    for (size_t type = 0; type < s_distinct.size(); ++type)
        if (const uint32_t cCount = s_distinct[type].load(std::memory_order_relaxed))
            forms[std::to_string(type)] = cCount;
    return {{"seconds", (NowMs() - s_windowStart.load()) / 1000.0},
            {"buckets", buckets},
            {"forms", forms},
            {"full", s_full.load(std::memory_order_relaxed)}};
}

void Reset()
{
    // Keys stay claimed; a change counted during the reset may land in either window.
    for (auto& bucket : s_buckets)
    {
        bucket.Calls.store(0, std::memory_order_relaxed);
        bucket.Recorded.store(0, std::memory_order_relaxed);
        for (auto& sample : bucket.Samples)
            sample.store(0, std::memory_order_relaxed);
    }
    for (auto& form : s_forms)
        form.store(0, std::memory_order_relaxed);
    for (auto& count : s_distinct)
        count.store(0, std::memory_order_relaxed);
    s_full.store(false, std::memory_order_relaxed);
    s_windowStart.store(NowMs());
}
} // namespace TestHarness::ChangeLog

namespace TestHarness
{
std::span<const Command> ChangeLogCommands()
{
    return ChangeLog::cCommands;
}
} // namespace TestHarness
