#pragma once

#include "MessageTrail.h"

#include <cstdint>
#include <vector>

// A game's performance over the last minute (docs/design/testing.md, Profiling): in every bug report's snapshot and
// the harness's status, player builds included. No engine.
namespace Diagnostics
{
// Each frame's length and our update's share of it, newest kept.
struct FrameTimes
{
    explicit FrameTimes(size_t aCapacity);

    void Add(int64_t aTimeMs, float aFrameMs, float aUpdateMs) noexcept;

    struct Summary
    {
        size_t Frames{};
        double Seconds{}; // from the first frame's end to the newest's
        float FrameP50{}, FrameP95{}, FrameP99{}, FrameMax{};
        float UpdateP50{}, UpdateP95{}, UpdateMax{};
    };
    // The frames that ended within aWindowMs of the newest.
    [[nodiscard]] Summary Last(int64_t aWindowMs) const;

  private:
    struct Sample
    {
        int64_t TimeMs;
        float FrameMs;
        float UpdateMs;
    };
    std::vector<Sample> m_samples;
    size_t m_next = 0;
    size_t m_count = 0;
};

// The main thread's (World::Update).
FrameTimes& Frames() noexcept;

// Serialized application messages per connection and direction, before transport compression.
struct TrafficRow
{
    const char* Name;
    MessageWay Way;
    uint32_t Count;
    uint64_t Bytes;
    uint64_t Peer{};
    bool Local{};
};
// The entries at or after aSinceMs by name and way, the most bytes first.
[[nodiscard]] std::vector<TrafficRow> Traffic(const std::vector<MessageEntry>& acEntries, int64_t aSinceMs);
} // namespace Diagnostics
