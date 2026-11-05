#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <unordered_map>

namespace TestHarness
{
// What shakes, frame by frame, since the last start: every loaded actor's place, drawn root, pelvis and heading turning
// back on themselves, and the things all of a screen hangs on (the hour, the player, the clock speed). Game thread.
struct JitterWatch
{
    void Start() noexcept;
    void Stop() noexcept { m_running = false; }
    // Once a frame, in the world.
    void Poll();
    [[nodiscard]] nlohmann::json Read() const;

private:
    // Steps along one quantity: how far, and how often a step undid the last.
    struct Track
    {
        float Last[3]{};
        float Step[3]{};
        bool Started = false;
        float Path = 0.f;
        float First[3]{};
        uint32_t Reversals = 0;
        float Biggest = 0.f;

        void Add(const float* acValue) noexcept;
        [[nodiscard]] nlohmann::json Json() const;
    };
    struct Watched
    {
        uint32_t Frames = 0;
        Track Place, Root, Pelvis, Heading;
        float GoalOff = 0.f;
        uint32_t Stepped = 0;
        uint32_t Furniture = 0;    // frames with its sit/sleep state set
        float FurniturePath = 0.f; // how far its place went in them
    };

    bool m_running = false;
    std::unordered_map<uint32_t, Watched> m_actors;
    Track m_player, m_camera;
    uint32_t m_frames = 0;
    uint32_t m_hourBack = 0;
    float m_hour = -1.f;
    float m_biggestHourBack = 0.f;
    uint32_t m_speedChanges = 0;
    float m_speed = -1.f;
    uint32_t m_weatherChanges = 0;
    const void* m_weather = nullptr;
};
} // namespace TestHarness
