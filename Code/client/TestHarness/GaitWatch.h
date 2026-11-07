#pragma once

#include "GaitMeasure.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <unordered_map>

namespace TestHarness
{
// Each loaded humanoid's walk over the last seconds, for coop_test.py's stuck-animation check: how far the actor
// travelled and how far its feet moved against its body while drawn (MeasureGait). A walk moves the feet about as far
// as the body goes; an actor sliding with no animation moves none, one in slow motion too little. Game thread.
struct GaitWatch
{
    // Once a frame, in the world.
    void Poll();
    // {id: {"travelled", "feet", "seconds", "frames", "hidden_frames"}} over the last aSeconds.
    [[nodiscard]] nlohmann::json Read(double aSeconds) const;

private:
    std::unordered_map<uint32_t, std::deque<GaitSample>> m_samples;
};
} // namespace TestHarness
