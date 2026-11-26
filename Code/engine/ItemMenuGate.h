#pragma once

#include <atomic>
#include <cstdint>

// A removal from a holder an open item menu lists, and the UI's calls that read the menu's lists (82082, 82083), kept
// apart: each waits for the other, bounded; the removal backs off when both start at once (item-menus.md, "The gate").
// Clock: a callable returning the elapsed microseconds since it was made; Pause: one wait step.
class ItemMenuGate
{
public:
    static constexpr uint64_t kWaitMicros = 5000;

    // The UI's call starts: waits for removals in flight. False: one still runs after kWaitMicros (it goes on anyway).
    template <class Clock, class Pause> [[nodiscard]] bool UiBegin(Clock&& aClock, Pause&& aPause) noexcept
    {
        m_reading.store(true, std::memory_order_seq_cst);
        while (m_inFlight.load(std::memory_order_seq_cst) > 0)
        {
            if (aClock() >= kWaitMicros)
                return false;
            aPause();
        }
        return true;
    }
    void UiEnd() noexcept { m_reading.store(false, std::memory_order_seq_cst); }

    // A listed removal starts: waits for the UI's call in progress, stepping back while it waits. False: still reading
    // after kWaitMicros (it goes on anyway).
    template <class Clock, class Pause> [[nodiscard]] bool RemovalBegin(Clock&& aClock, Pause&& aPause) noexcept
    {
        for (;;)
        {
            m_inFlight.fetch_add(1, std::memory_order_seq_cst);
            if (!m_reading.load(std::memory_order_seq_cst))
                return true;
            m_inFlight.fetch_sub(1, std::memory_order_seq_cst);
            while (m_reading.load(std::memory_order_seq_cst))
            {
                if (aClock() >= kWaitMicros)
                {
                    m_inFlight.fetch_add(1, std::memory_order_seq_cst);
                    return false;
                }
                aPause();
            }
        }
    }
    void RemovalEnd() noexcept { m_inFlight.fetch_sub(1, std::memory_order_seq_cst); }

    [[nodiscard]] bool Reading() const noexcept { return m_reading.load(std::memory_order_seq_cst); }
    [[nodiscard]] int32_t InFlight() const noexcept { return m_inFlight.load(std::memory_order_seq_cst); }

private:
    std::atomic<bool> m_reading{};
    std::atomic<int32_t> m_inFlight{};
};
