#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

// The game's playback time for copies (docs/design/copy-clock.md): the owners' time minus the line's lateness,
// steered toward it a little each frame, never back and never ahead. No engine: ClientTests.
class CopyClock
{
public:
    static constexpr uint64_t kWindowMs = 3000; // lateness kept this long: rises at once, falls when a spike leaves
    static constexpr uint64_t kMarginMs = 20;
    static constexpr uint64_t kMaxMs = 400;
    static constexpr uint64_t kSnapMs = 1000; // further behind than this: jumps (a start, a load, a long stall)
    static constexpr double kSteerMs = 1000.0;
    static constexpr double kSlowest = 0.85;
    static constexpr double kFastest = 1.1;

    // An owner's snapshot stamped aOwnerTick (the session's time) arrived at aArrived.
    void OnSnapshot(uint64_t aOwnerTick, uint64_t aArrived) noexcept;
    // Once a frame, the session's time now.
    void Advance(uint64_t aNow) noexcept;
    void Reset() noexcept;

    [[nodiscard]] uint64_t Now() const noexcept { return static_cast<uint64_t>(m_tick); }
    // The delay it steers toward: the window's worst lateness and a margin.
    [[nodiscard]] uint64_t Network() const noexcept;
    // How far behind the session's time it shows now.
    [[nodiscard]] uint64_t Delay() const noexcept { return m_now > Now() ? m_now - Now() : 0; }
    [[nodiscard]] uint64_t Frame() const noexcept { return m_frame; }
    [[nodiscard]] double FrameMs() const noexcept { return m_frameMs; }

private:
    std::deque<std::pair<uint64_t, uint64_t>> m_late; // (arrived, lateness), oldest first
    double m_tick{};
    uint64_t m_now{};
    uint64_t m_frame{};
    double m_frameMs{};
    bool m_started{};
};

// One copy's playback time: the game's (CopyClock) minus its owner's send interval for it, which changes with its
// rate (30/s for players and fights, 10/s otherwise) and is steered too, never jumped. No engine: ClientTests.
class CopyPlayback
{
public:
    static constexpr double kDefaultIntervalMs = 100.0;
    static constexpr uint64_t kLongestGapMs = 250; // a longer gap is a stall, not the rate
    static constexpr size_t kNewest = 4;
    static constexpr double kSteer = 0.1; // the interval changes by at most this much of a frame, per frame

    // A snapshot of it stamped aTick.
    void OnPoint(uint64_t aTick) noexcept;
    // Its time in aClock's current frame (stepped once a frame, whoever asks first); never back.
    uint64_t At(const CopyClock& acClock) noexcept;
    // A teleport or spawn: at its target at once, even back.
    void Snap(const CopyClock& acClock) noexcept;

    [[nodiscard]] double Interval() const noexcept { return m_interval; }
    // The interval it steers toward: the gaps' mean over the window, or the newest few's if longer.
    [[nodiscard]] double TargetInterval() const noexcept;

private:
    std::deque<std::pair<uint64_t, uint64_t>> m_gaps; // (tick, gap), oldest first
    uint64_t m_last{};
    double m_interval{kDefaultIntervalMs};
    uint64_t m_tick{};
    uint64_t m_frame{};
    bool m_started{};
};
