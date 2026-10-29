#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace Diagnostics
{
// The newest entries in memory, numbered as they come (T has uint64_t Seq and int64_t TimeMs, since the Unix epoch).
// Entries past aMaxEntries or older than aMaxAgeMs (than the newest) are dropped. Any thread. No engine.
template <class T> struct Trail
{
    Trail(size_t aMaxEntries, int64_t aMaxAgeMs) noexcept : m_maxEntries(aMaxEntries), m_maxAgeMs(aMaxAgeMs)
    {
    }

    void Add(T aEntry) noexcept
    {
        std::scoped_lock _{m_lock};
        aEntry.Seq = m_next++;
        const int64_t cTimeMs = aEntry.TimeMs;
        m_entries.push_back(std::move(aEntry));
        while (m_entries.size() > m_maxEntries || cTimeMs - m_entries.front().TimeMs > m_maxAgeMs)
            m_entries.pop_front();
    }

    // The entries numbered aSince and later, oldest first.
    [[nodiscard]] std::vector<T> Since(uint64_t aSince = 0) const noexcept
    {
        std::scoped_lock _{m_lock};
        std::vector<T> entries;
        for (const auto& cEntry : m_entries)
            if (cEntry.Seq >= aSince)
                entries.push_back(cEntry);
        return entries;
    }

    // The number the next entry gets.
    [[nodiscard]] uint64_t Next() const noexcept
    {
        std::scoped_lock _{m_lock};
        return m_next;
    }

  private:
    const size_t m_maxEntries;
    const int64_t m_maxAgeMs;
    mutable std::mutex m_lock;
    std::deque<T> m_entries;
    uint64_t m_next = 0;
};
} // namespace Diagnostics
