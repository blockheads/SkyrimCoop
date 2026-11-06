#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace TestHarness
{
// The world around a spot as a walking player meets it (docs/re/systems/terrain.md): a grid of columns, each every
// surface a downward ray meets (layer 30) with its slope and the headroom above the standable ones, plus the
// navmesh drawn into the same grid. Rays have a time budget a frame (bhkWorld::PickObject 77860), so a scan runs over
// frames: Start gives a ticket, Result reads it once done. Game thread.
struct TerrainScan
{
    // {"at": [x, y, z] (the player's feet by default), "radius": 1000, "cell": 50, "up": 600, "down": 1500,
    // "toward": id}: starts a scan (the one before is dropped); its ticket.
    uint32_t Start(const nlohmann::json& acRequest) noexcept;
    // Once a frame, in the world.
    void Poll() noexcept;
    // {"done": false, "progress"} while it runs; the scan once done; none for another ticket.
    [[nodiscard]] nlohmann::json Result(uint32_t aTicket) const;

private:
    struct Hit
    {
        float Z;
        float NormalZ;
        uint32_t Reference;
        uint32_t Layer;
        float Headroom; // standable ones (NormalZ > 0.5): to the first thing above, -1 nothing within the check
    };
    bool ScanColumn(size_t aIndex) noexcept;
    nlohmann::json Finish() const;

    uint32_t m_ticket = 0;
    bool m_running = false;
    std::array<float, 3> m_at{};
    float m_cell = 50.f, m_up = 600.f, m_down = 1500.f, m_west = 0.f, m_north = 0.f;
    int m_cols = 0, m_rows = 0;
    uint32_t m_toward = 0;
    size_t m_next = 0;
    uint32_t m_frames = 0;
    std::vector<std::vector<Hit>> m_columns;
    nlohmann::json m_result;
};
} // namespace TestHarness
