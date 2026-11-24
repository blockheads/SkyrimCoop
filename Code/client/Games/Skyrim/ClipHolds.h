#pragma once

#include <chrono>
#include <mutex>
#include <unordered_map>

// The objects whose graphs the host keeps updating while out of its camera's sight: a script animated them lately and
// may wait for the clip's end. Any thread; its lock is a leaf. Checked by ClientTests.
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
    }
    [[nodiscard]] bool Held(TKey aKey, TimePoint aNow) noexcept
    {
        std::scoped_lock _{m_lock};
        const auto it = m_until.find(aKey);
        return it != m_until.end() && it->second > aNow;
    }
    void Clear() noexcept
    {
        std::scoped_lock _{m_lock};
        m_until.clear();
    }

  private:
    std::mutex m_lock;
    std::unordered_map<TKey, TimePoint> m_until;
};
