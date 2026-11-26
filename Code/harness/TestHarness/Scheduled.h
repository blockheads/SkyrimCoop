#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

namespace TestHarness
{
struct Harness;

// Commands held until a moment on this game's steady clock: both games' inputs land in one frame (together,
// docs/design/repro-mcp.md). Game thread only.
struct Scheduled
{
    [[nodiscard]] static double NowMs() noexcept;

    // A request with "at" (ms on NowMs): runs in the first frame at or past it; returns its id.
    uint32_t Queue(nlohmann::json aRequest, double aAtMs);
    // Every frame, before the socket's requests: counts the frame, then runs what is due.
    void Poll(Harness& aHarness);
    // That command once it ran ({done, frame, ms, late_ms, result}), handed out once; {done: false} before.
    [[nodiscard]] nlohmann::json Take(uint32_t aId);

    uint64_t Frame = 0;

private:
    struct Entry
    {
        uint32_t Id;
        double AtMs;
        nlohmann::json Request;
    };
    std::vector<Entry> m_waiting;
    nlohmann::json m_done = nlohmann::json::object();
    uint32_t m_nextId = 1;
};
} // namespace TestHarness
