#include <Diagnostics/Counters.h>

#include <deque>
#include <mutex>

namespace Diagnostics::Counters
{
namespace
{
std::mutex s_lock;
std::deque<Counter>& All() noexcept
{
    static std::deque<Counter> s_counters; // a deque: addresses stay put as it grows
    return s_counters;
}
} // namespace

Counter& Get(const char* acpName) noexcept
{
    std::scoped_lock _(s_lock);
    for (auto& counter : All())
        if (counter.Name == acpName)
            return counter;
    auto& counter = All().emplace_back();
    counter.Name = acpName;
    return counter;
}

std::vector<Reading> Read() noexcept
{
    std::scoped_lock _(s_lock);
    std::vector<Reading> readings;
    for (const auto& cCounter : All())
        readings.push_back({cCounter.Name, cCounter.Count.load(), cCounter.Sum.load(), cCounter.Max.load()});
    return readings;
}

void Reset() noexcept
{
    std::scoped_lock _(s_lock);
    for (auto& counter : All())
    {
        counter.Count = 0;
        counter.Sum = 0;
        counter.Max = 0;
    }
}
} // namespace Diagnostics::Counters
