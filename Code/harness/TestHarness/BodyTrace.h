#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <set>

namespace TestHarness
{
// Records watched references' physics frame by frame, for coop_test.py's body traces (how a guest's playback
// follows the host's flight): each frame, the 3D's world position (what the player sees) and each body's position,
// motion type and awake. Stamped with the wall clock (both games on one machine line up). Game thread.
struct BodyTrace
{
    // Starts recording these references (adds them when already recording).
    void Watch(const std::set<uint32_t>& acReferences);
    // What was recorded since the last take, in order. Keeps recording.
    nlohmann::json Take();
    nlohmann::json Stop();
    // Once a frame.
    void Poll();
    bool Recording() const noexcept { return !m_references.empty(); }

private:
    std::set<uint32_t> m_references;
    nlohmann::json m_entries = nlohmann::json::array();
};
} // namespace TestHarness
