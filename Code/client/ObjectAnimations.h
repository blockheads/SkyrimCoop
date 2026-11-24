#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <utility>

// The host's scripts' animations on a guest's objects, in order: a script waited for the clip before its next one
// (PlayAnimationAndWait 56206), and a graph still in that clip refuses the next. Kept until its graph takes it, no
// longer than kWait. aSend(animation) is the graph's answer. Checked by ClientTests.
template <class TObject, class TAnimation> class ObjectAnimations
{
  public:
    using TimePoint = std::chrono::steady_clock::time_point;
    static constexpr std::chrono::seconds kWait{3};

    template <class TSend> void Play(const TObject& acObject, TAnimation aEvent, TimePoint aNow, TSend&& aSend)
    {
        auto& waiting = m_waiting[acObject];
        waiting.emplace_back(std::move(aEvent), aNow);
        Drain(waiting, aNow, aSend);
        if (waiting.empty())
            m_waiting.erase(acObject);
    }

    template <class TSend> void Update(TimePoint aNow, TSend&& aSend)
    {
        for (auto& [cObject, waiting] : m_waiting)
            Drain(waiting, aNow, aSend);
        std::erase_if(m_waiting, [](const auto& acEntry) { return acEntry.second.empty(); });
    }

    void Clear() noexcept { m_waiting.clear(); }
    [[nodiscard]] bool Empty() const noexcept { return m_waiting.empty(); }

  private:
    using Queue = std::deque<std::pair<TAnimation, TimePoint>>;

    template <class TSend> static void Drain(Queue& aWaiting, TimePoint aNow, TSend& aSend)
    {
        while (!aWaiting.empty() && (aSend(aWaiting.front().first) || aNow - aWaiting.front().second >= kWait))
            aWaiting.pop_front();
    }

    std::map<TObject, Queue> m_waiting;
};
