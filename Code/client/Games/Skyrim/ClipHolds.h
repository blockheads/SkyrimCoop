#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>

// The objects whose graphs the host keeps updating while out of its camera's sight: a script animated them lately and
// may wait for the clip's end. Any thread; its lock is a leaf. Checked by UnitTests.
template <class TKey> class ClipHolds
{
  public:
    using TimePoint = std::chrono::steady_clock::time_point;
    // Longer than a trap's clip; a looping trap's next animation holds again.
    static constexpr std::chrono::seconds kHeld{15};

    void Hold(TKey aKey, TimePoint aNow) noexcept
    {
        std::scoped_lock _{m_lock};
        std::erase_if(m_until, [aNow](const auto& acEntry) { return acEntry.second <= aNow; });
        m_until[aKey] = aNow + kHeld;
        const auto cEnd = (aNow + kHeld).time_since_epoch().count();
        for (auto last = m_last.load(std::memory_order_relaxed); last < cEnd && !m_last.compare_exchange_weak(last, cEnd);)
        {
        }
    }
    // Lock-free once every hold has ended: the engine asks for each object each frame, from its job threads.
    [[nodiscard]] bool Held(TKey aKey, TimePoint aNow) noexcept
    {
        if (aNow.time_since_epoch().count() >= m_last.load(std::memory_order_relaxed))
            return false;
        std::scoped_lock _{m_lock};
        const auto it = m_until.find(aKey);
        return it != m_until.end() && it->second > aNow;
    }
    void Clear() noexcept
    {
        std::scoped_lock _{m_lock};
        m_until.clear();
        m_last.store(0, std::memory_order_relaxed);
    }

  private:
    std::mutex m_lock;
    std::unordered_map<TKey, TimePoint> m_until;
    std::atomic<TimePoint::rep> m_last{0}; // when the latest hold ends
};
