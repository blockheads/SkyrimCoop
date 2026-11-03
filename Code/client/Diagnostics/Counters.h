#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Named counters any code can bump, hooks included (atomic, no lock after the first use): a count, a sum and a max,
// read by the harness's status and `profile` (docs/design/repro-mcp.md, phase 2). For measurements that used to need
// temporary code: COUNT("clamp cut ms", cut), or COUNT_TIME("graph state save") around a call.
namespace Diagnostics::Counters
{
struct Counter
{
    std::string Name;
    std::atomic<uint64_t> Count{};
    std::atomic<uint64_t> Sum{};
    std::atomic<uint64_t> Max{};
};

// The counter of that name, made on first use; its address stays valid.
Counter& Get(const char* acpName) noexcept;

inline void Add(Counter& aCounter, uint64_t aValue) noexcept
{
    aCounter.Count.fetch_add(1, std::memory_order_relaxed);
    aCounter.Sum.fetch_add(aValue, std::memory_order_relaxed);
    for (uint64_t max = aCounter.Max.load(std::memory_order_relaxed);
         aValue > max && !aCounter.Max.compare_exchange_weak(max, aValue, std::memory_order_relaxed);)
    {
    }
}

struct Reading
{
    std::string Name;
    uint64_t Count;
    uint64_t Sum;
    uint64_t Max;
};
[[nodiscard]] std::vector<Reading> Read() noexcept;
void Reset() noexcept;

// Adds the scope's time, in microseconds.
struct ScopedTime
{
    explicit ScopedTime(Counter& aCounter) noexcept
        : m_counter(aCounter)
    {
    }
    ~ScopedTime()
    {
        Add(m_counter, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - m_start).count()));
    }
    ScopedTime(const ScopedTime&) = delete;
    ScopedTime& operator=(const ScopedTime&) = delete;

private:
    Counter& m_counter;
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};
} // namespace Diagnostics::Counters

#define COUNTER_CONCAT2(a, b) a##b
#define COUNTER_CONCAT(a, b) COUNTER_CONCAT2(a, b)
#define COUNT(acpName, aValue)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        static auto& s_counter = ::Diagnostics::Counters::Get(acpName);                                                \
        ::Diagnostics::Counters::Add(s_counter, static_cast<uint64_t>(aValue));                                        \
    } while (0)
#define COUNT_TIME(acpName)                                                                                            \
    static auto& COUNTER_CONCAT(s_timeCounter, __LINE__) = ::Diagnostics::Counters::Get(acpName);                      \
    const ::Diagnostics::Counters::ScopedTime COUNTER_CONCAT(timeScope, __LINE__)(COUNTER_CONCAT(s_timeCounter, __LINE__))
