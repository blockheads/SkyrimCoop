#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>

namespace TestHarness
{
// The plugin's log lines (info and up) as they are written, numbered, the last 4000 kept: what each game did
// around a scenario's failing step, without opening its log file (rotated, flushed once a second).
struct LogTrail
{
    // Joins the default logger once it exists. Once a frame.
    void Poll() noexcept;
    // The lines numbered aSince and later, and the next number.
    nlohmann::json Since(uint64_t aSince) const;
};
} // namespace TestHarness
