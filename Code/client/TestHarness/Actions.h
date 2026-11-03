#pragma once

#include "ActionSteer.h"
#include "EngineCalls.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace TestHarness
{
struct Harness;

// A checked action (docs/design/checked-actions.md): the player acts frame by frame in the game, not from Python
// between frames. It waits until ready, turns and holds buttons each frame, fails on the frame a kept condition
// breaks, ends when its until conditions hold, and records watched values. One at a time. Game thread.
class Actions
{
public:
    // {"cmd": "act", ...}: starts one (a running one is cancelled). The spec's errors refuse it.
    nlohmann::json Start(Harness& aHarness, const nlohmann::json& acSpec);
    // Its phase, and once ended its outcome.
    nlohmann::json State() const;
    void Cancel(Harness& aHarness);
    // Once a frame, in the world.
    void Poll(Harness& aHarness);

private:
    struct Point
    {
        uint32_t Ref = 0; // 0x14 the player
        std::optional<std::array<float, 3>> At;
    };
    struct Condition
    {
        std::string Kind;
        Point A, B;
        float Value = 0.f;
        uint32_t Id = 0;
        bool Flag = true;
    };
    struct Watch
    {
        std::string Name, Kind;
        Point A, B;
        ActionSteer::Extremes Seen;
        std::optional<float> Last;
    };
    enum class Phase
    {
        Idle,
        Ready,
        Running,
        Ended
    };

    [[nodiscard]] bool Parse(const nlohmann::json& acSpec, std::string& aError);
    [[nodiscard]] static bool ParsePoint(const nlohmann::json& acValue, Point& aPoint);
    [[nodiscard]] static bool ParseCondition(const nlohmann::json& acValue, Condition& aCondition, std::string& aError);
    [[nodiscard]] static std::optional<std::array<float, 3>> PlaceOf(const Point& acPoint);
    // Whether it holds now, and what was read (for a failure's reason).
    [[nodiscard]] bool Holds(const Condition& acCondition, std::string& aRead) const;
    [[nodiscard]] std::optional<float> Measure(const Watch& acWatch) const;
    [[nodiscard]] std::optional<std::array<float, 2>> AimGoal() const;
    void End(Harness& aHarness, const std::string& acStatus, const std::string& acReason);
    void Release(Harness& aHarness);
    [[nodiscard]] nlohmann::json Watched() const;

    Phase m_phase = Phase::Idle;
    uint32_t m_id = 0;
    std::string m_name;
    std::vector<Condition> m_ready, m_until, m_keep;
    std::vector<Watch> m_watches;
    std::vector<EngineCalls::Button> m_buttons;
    // Turning: to fixed angles, or toward a reference each frame (its yaw, and its pitch unless fixed).
    std::optional<float> m_yaw, m_pitch;
    Point m_face;
    bool m_hasFace = false;
    float m_rate = 60.f; // degrees a second at most (a sudden turn drops a held object)
    float m_aimTolerance = 0.5f;
    ActionSteer::Axis m_yawAxis, m_pitchAxis;
    bool m_allowMenus = false;
    bool m_series = false;
    double m_readySeconds = 5.0, m_seconds = 20.0;
    // Its clocks: real time since it started, game time (frames it ran, not paused) since it ran.
    std::chrono::steady_clock::time_point m_started{}, m_lastPoll{};
    double m_runSeconds = 0.0;
    uint64_t m_frames = 0;
    std::array<float, 3> m_playerStart{};
    std::vector<std::pair<uint32_t, std::array<float, 3>>> m_starts; // moved_less's references at the start
    nlohmann::json m_outcome;
    nlohmann::json m_seriesRows = nlohmann::json::array();
};
} // namespace TestHarness
