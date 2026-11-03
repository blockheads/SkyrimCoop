#include "Actions.h"

#include "Commands.h"
#include "Harness.h"


#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace TestHarness
{
using namespace EngineCalls;

namespace
{
constexpr uint32_t cPlayer = 0x14;
constexpr size_t cSeriesLimit = 4000;

double Since(std::chrono::steady_clock::time_point aStart) noexcept
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - aStart).count();
}

// No menu but the HUD, no kill cam: a fade, a loading screen or the console swallows a player's input.
bool TakesInput() noexcept
{
    for (const auto& cMenu : GetOpenMenus())
        if (cMenu != "HUD Menu")
            return false;
    return !IsKillCamPlaying();
}

float DistanceXy(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return std::hypot(a[0] - b[0], a[1] - b[1]);
}

float Distance(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

std::string Round(const std::optional<std::array<float, 3>>& acAt)
{
    return acAt ? std::format("({:.0f}, {:.0f}, {:.0f})", (*acAt)[0], (*acAt)[1], (*acAt)[2]) : "nowhere";
}
} // namespace

bool Actions::ParsePoint(const nlohmann::json& acValue, Point& aPoint)
{
    if (acValue.is_string() && acValue.get<std::string>() == "player")
        aPoint.Ref = cPlayer;
    else if (acValue.is_number_unsigned() || acValue.is_number_integer())
        aPoint.Ref = acValue.get<uint32_t>();
    else if (acValue.is_array() && acValue.size() == 3)
        aPoint.At = std::array{acValue[0].get<float>(), acValue[1].get<float>(), acValue[2].get<float>()};
    else
        return false;
    return true;
}

bool Actions::ParseCondition(const nlohmann::json& acValue, Condition& aCondition, std::string& aError)
{
    if (acValue.is_string())
    {
        aCondition.Kind = acValue.get<std::string>();
        if (aCondition.Kind == "on_ground" || aCondition.Kind == "takes_input" || aCondition.Kind == "aimed")
            return true;
        aError = "unknown condition " + aCondition.Kind;
        return false;
    }
    if (!acValue.is_object() || acValue.size() != 1)
    {
        aError = "a condition is a name or {name: arguments}: " + acValue.dump();
        return false;
    }
    const auto& [cKind, cArgs] = *acValue.items().begin();
    aCondition.Kind = cKind;
    if (cKind == "on_ground" || cKind == "takes_input")
        aCondition.Flag = cArgs.get<bool>();
    else if (cKind == "holding")
        aCondition.Id = cArgs.get<uint32_t>(); // 0: holds nothing
    else if (cKind == "aimed" || cKind == "for")
        aCondition.Value = cArgs.get<float>();
    else if (cKind == "near" || cKind == "near_xy" || cKind == "far" || cKind == "far_xy")
    {
        if (!cArgs.is_array() || cArgs.size() != 3 || !ParsePoint(cArgs[0], aCondition.A) || !ParsePoint(cArgs[1], aCondition.B))
        {
            aError = cKind + " takes [a, b, distance]";
            return false;
        }
        aCondition.Value = cArgs[2].get<float>();
    }
    else if (cKind == "moved_less")
    {
        if (!cArgs.is_array() || cArgs.size() != 2 || !ParsePoint(cArgs[0], aCondition.A))
        {
            aError = "moved_less takes [reference, distance]";
            return false;
        }
        aCondition.Value = cArgs[1].get<float>();
    }
    else
    {
        aError = "unknown condition " + cKind;
        return false;
    }
    return true;
}

bool Actions::Parse(const nlohmann::json& acSpec, std::string& aError)
{
    *this = Actions{};
    m_name = acSpec.value("name", std::string("act"));
    const auto cConditions = [&](const char* acpKey, std::vector<Condition>& aInto) {
        for (const auto& cValue : acSpec.value(acpKey, nlohmann::json::array()))
        {
            Condition condition;
            if (!ParseCondition(cValue, condition, aError))
                return false;
            aInto.push_back(condition);
        }
        return true;
    };
    if (!cConditions("ready", m_ready) || !cConditions("until", m_until) || !cConditions("keep", m_keep))
        return false;
    for (const auto& cValue : acSpec.value("watch", nlohmann::json::array()))
    {
        Watch watch;
        watch.Name = cValue.value("name", std::string());
        for (const char* cpKind : {"distance", "distance_xy", "z", "z_between"})
            if (cValue.contains(cpKind))
                watch.Kind = cpKind;
        const auto& cArgs = cValue[watch.Kind.empty() ? "distance" : watch.Kind];
        const bool cPair = watch.Kind != "z";
        if (watch.Kind.empty() || watch.Name.empty() ||
            (cPair ? !(cArgs.is_array() && cArgs.size() == 2 && ParsePoint(cArgs[0], watch.A) && ParsePoint(cArgs[1], watch.B))
                   : !ParsePoint(cArgs, watch.A)))
        {
            aError = "a watch is {name, distance|distance_xy|z_between: [a, b]} or {name, z: a}: " + cValue.dump();
            return false;
        }
        m_watches.push_back(watch);
    }
    for (const auto& cValue : acSpec.value("buttons", nlohmann::json::array()))
    {
        const auto cButton = PlayerInput::ButtonByName(cValue.get<std::string>());
        if (!cButton)
        {
            aError = "unknown button " + cValue.dump();
            return false;
        }
        m_buttons.push_back(*cButton);
    }
    if (acSpec.contains("turn"))
    {
        const auto& cTurn = acSpec["turn"];
        if (cTurn.contains("yaw"))
            m_yaw = cTurn["yaw"].get<float>();
        if (cTurn.contains("pitch"))
            m_pitch = cTurn["pitch"].get<float>();
        if (cTurn.contains("face"))
        {
            if (!ParsePoint(cTurn["face"], m_face))
            {
                aError = "turn.face is a reference or a point";
                return false;
            }
            m_hasFace = true;
        }
        m_rate = cTurn.value("rate", m_rate);
        m_aimTolerance = cTurn.value("tolerance", m_aimTolerance);
    }
    m_allowMenus = acSpec.value("allow_menus", false);
    m_series = acSpec.value("series", false);
    m_readySeconds = acSpec.value("ready_seconds", m_readySeconds);
    m_seconds = acSpec.value("seconds", m_seconds);
    return true;
}

std::optional<std::array<float, 3>> Actions::PlaceOf(const Point& acPoint)
{
    if (acPoint.At)
        return acPoint.At;
    return ReferencePlace(acPoint.Ref);
}

std::optional<std::array<float, 2>> Actions::AimGoal() const
{
    std::optional<float> yaw = m_yaw, pitch = m_pitch;
    if (m_hasFace)
    {
        const auto cMe = ReferencePlace(cPlayer);
        const auto cTarget = PlaceOf(m_face);
        if (!cMe || !cTarget)
            return std::nullopt;
        // From the eyes to an actor's chest or to an object itself (EngineCalls::FacePlayerToward).
        const bool cActor = m_face.Ref && GetCharacterState(m_face.Ref) >= 0;
        const float cDx = (*cTarget)[0] - (*cMe)[0], cDy = (*cTarget)[1] - (*cMe)[1];
        const float cDz = ((*cMe)[2] + 115.f) - ((*cTarget)[2] + (cActor ? 85.f : 0.f));
        yaw = glm::degrees(std::atan2(cDx, cDy));
        if (!pitch)
            pitch = glm::degrees(std::atan2(cDz, std::hypot(cDx, cDy)));
    }
    const auto cAt = PlayerAngles();
    if (!cAt)
        return std::nullopt;
    return std::array{yaw.value_or((*cAt)[0]), pitch.value_or((*cAt)[1])};
}

bool Actions::Holds(const Condition& acCondition, std::string& aRead) const
{
    const std::string& cKind = acCondition.Kind;
    if (cKind == "on_ground")
    {
        const int32_t cState = GetCharacterState(cPlayer);
        aRead = std::format("character state {}", cState);
        return (cState == 0) == acCondition.Flag;
    }
    if (cKind == "takes_input")
    {
        const bool cTakes = TakesInput();
        aRead = cTakes ? "takes input" : "a menu, fade or kill cam has the input";
        return cTakes == acCondition.Flag;
    }
    if (cKind == "holding")
    {
        const uint32_t cHeld = GetGrabbedObject();
        aRead = cHeld ? std::format("holds {:X}", cHeld) : "holds nothing";
        return cHeld == acCondition.Id;
    }
    if (cKind == "aimed")
    {
        const auto cGoal = AimGoal();
        const auto cAt = PlayerAngles();
        if (!cGoal || !cAt)
        {
            aRead = "no aim";
            return false;
        }
        const float cTolerance = acCondition.Value > 0.f ? acCondition.Value : m_aimTolerance;
        const float cYaw = ActionSteer::AngleError((*cGoal)[0], (*cAt)[0]), cPitch = ActionSteer::AngleError((*cGoal)[1], (*cAt)[1]);
        aRead = std::format("{:.1f} degrees off in yaw, {:.1f} in pitch", cYaw, cPitch);
        return std::fabs(cYaw) <= cTolerance && std::fabs(cPitch) <= cTolerance;
    }
    if (cKind == "for")
    {
        aRead = std::format("{:.2f} s of {:.2f}", m_runSeconds, acCondition.Value);
        return m_runSeconds >= acCondition.Value;
    }
    if (cKind == "moved_less")
    {
        const auto cNow = PlaceOf(acCondition.A);
        const auto it = std::ranges::find(m_starts, acCondition.A.Ref, &std::pair<uint32_t, std::array<float, 3>>::first);
        if (!cNow || it == m_starts.end())
        {
            aRead = std::format("{:X} is gone", acCondition.A.Ref);
            return false;
        }
        const float cMoved = Distance(*cNow, it->second);
        aRead = std::format("{:X} moved {:.1f}", acCondition.A.Ref, cMoved);
        return cMoved < acCondition.Value;
    }
    // near, near_xy, far, far_xy
    const auto cA = PlaceOf(acCondition.A), cB = PlaceOf(acCondition.B);
    if (!cA || !cB)
    {
        aRead = std::format("{} or {} is gone", Round(cA), Round(cB));
        return false;
    }
    const bool cXy = cKind.ends_with("_xy");
    const float cApart = cXy ? DistanceXy(*cA, *cB) : Distance(*cA, *cB);
    aRead = std::format("{:.1f} apart{} ({} and {})", cApart, cXy ? " across" : "", Round(cA), Round(cB));
    return cKind.starts_with("near") ? cApart <= acCondition.Value : cApart >= acCondition.Value;
}

std::optional<float> Actions::Measure(const Watch& acWatch) const
{
    const auto cA = PlaceOf(acWatch.A);
    if (!cA)
        return std::nullopt;
    if (acWatch.Kind == "z")
        return (*cA)[2];
    const auto cB = PlaceOf(acWatch.B);
    if (!cB)
        return std::nullopt;
    if (acWatch.Kind == "z_between")
        return (*cA)[2] - (*cB)[2];
    return acWatch.Kind == "distance_xy" ? DistanceXy(*cA, *cB) : Distance(*cA, *cB);
}

nlohmann::json Actions::Start(Harness& aHarness, const nlohmann::json& acSpec)
{
    if (m_phase == Phase::Ready || m_phase == Phase::Running)
        Cancel(aHarness);
    const uint32_t cId = m_id + 1;
    std::string error;
    if (!Parse(acSpec, error))
    {
        m_id = cId;
        return Error(error);
    }
    m_id = cId;
    m_phase = Phase::Ready;
    m_started = m_lastPoll = std::chrono::steady_clock::now();
    spdlog::info("Action {} '{}': waiting until ready", m_id, m_name);
    return {{"ok", true}, {"id", m_id}};
}

void Actions::Release(Harness& aHarness)
{
    for (const auto cButton : m_buttons)
        (void)aHarness.Input.Set(cButton, false);
}

nlohmann::json Actions::Watched() const
{
    nlohmann::json watched = nlohmann::json::object();
    for (const auto& cWatch : m_watches)
        watched[cWatch.Name] = {{"min", cWatch.Seen.Min ? nlohmann::json(*cWatch.Seen.Min) : nlohmann::json()},
                                {"max", cWatch.Seen.Max ? nlohmann::json(*cWatch.Seen.Max) : nlohmann::json()},
                                {"min_frame", cWatch.Seen.MinFrame}, {"max_frame", cWatch.Seen.MaxFrame},
                                {"last", cWatch.Last ? nlohmann::json(*cWatch.Last) : nlohmann::json()}};
    return watched;
}

void Actions::End(Harness& aHarness, const std::string& acStatus, const std::string& acReason)
{
    if (m_phase == Phase::Running)
        Release(aHarness);
    m_phase = Phase::Ended;
    m_outcome = {{"status", acStatus}, {"reason", acReason}, {"frame", m_frames}, {"seconds", m_runSeconds},
                 {"real_seconds", Since(m_started)}, {"watched", Watched()}};
    if (m_series)
        m_outcome["series"] = std::move(m_seriesRows);
    spdlog::info("Action {} '{}': {} after {} frames ({:.2f} s){}{}", m_id, m_name, acStatus, m_frames, m_runSeconds,
                 acReason.empty() ? "" : ": ", acReason);
}

void Actions::Cancel(Harness& aHarness)
{
    if (m_phase == Phase::Ready || m_phase == Phase::Running)
        End(aHarness, "cancelled", "");
}

nlohmann::json Actions::State() const
{
    static constexpr const char* cPhases[] = {"idle", "ready", "running", "ended"};
    nlohmann::json state = {{"ok", true}, {"id", m_id}, {"phase", cPhases[static_cast<int>(m_phase)]}, {"frame", m_frames},
                            {"seconds", m_runSeconds}};
    if (m_phase == Phase::Ended)
        state["outcome"] = m_outcome;
    else if (m_phase == Phase::Running)
        state["watched"] = Watched();
    return state;
}

void Actions::Poll(Harness& aHarness)
{
    if (m_phase != Phase::Ready && m_phase != Phase::Running)
        return;
    const auto cNow = std::chrono::steady_clock::now();
    const double cDelta = std::chrono::duration<double>(cNow - m_lastPoll).count();
    m_lastPoll = cNow;
    std::string read;

    if (m_phase == Phase::Ready)
    {
        for (const auto& cCondition : m_ready)
            if (!Holds(cCondition, read))
            {
                if (Since(m_started) > m_readySeconds)
                    End(aHarness, "not_ready", std::format("{} never held: {}", cCondition.Kind, read));
                return;
            }
        m_phase = Phase::Running;
        if (const auto cMe = ReferencePlace(cPlayer))
            m_playerStart = *cMe;
        for (const auto& cCondition : m_keep)
            if (cCondition.Kind == "moved_less")
                if (const auto cAt = PlaceOf(cCondition.A))
                    m_starts.emplace_back(cCondition.A.Ref, *cAt);
        for (const auto cButton : m_buttons)
            (void)aHarness.Input.Set(cButton, true);
        spdlog::info("Action {} '{}': ready after {:.2f} s, running", m_id, m_name, Since(m_started));
        return;
    }

    // Paused or in a menu: no frame of the action, no time on its clock (OpenMW's tests skip a paused world).
    if (!m_allowMenus && !TakesInput())
        return;
    ++m_frames;
    m_runSeconds += cDelta;

    for (auto& watch : m_watches)
        if ((watch.Last = Measure(watch)))
            watch.Seen.Add(*watch.Last, m_frames);
    if (m_series && m_seriesRows.size() < cSeriesLimit)
    {
        nlohmann::json row = {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()};
        for (const auto& cWatch : m_watches)
            row.push_back(cWatch.Last ? nlohmann::json(*cWatch.Last) : nlohmann::json());
        m_seriesRows.push_back(std::move(row));
    }

    for (const auto& cCondition : m_keep)
        if (!Holds(cCondition, read))
        {
            End(aHarness, "failed", std::format("frame {} ({:.2f} s): {} broke: {}", m_frames, m_runSeconds, cCondition.Kind, read));
            return;
        }
    if (!m_until.empty() && std::ranges::all_of(m_until, [&](const Condition& acCondition) { std::string unused; return Holds(acCondition, unused); }))
    {
        End(aHarness, "done", "");
        return;
    }
    if (m_runSeconds > m_seconds)
    {
        std::string why;
        for (const auto& cCondition : m_until)
            if (!Holds(cCondition, read))
            {
                why = std::format("{} not reached: {}", cCondition.Kind, read);
                break;
            }
        End(aHarness, m_until.empty() ? "done" : "timeout", why);
        return;
    }

    // Steer: mouse counts toward the aim, at most m_rate degrees a second.
    if (m_yaw || m_pitch || m_hasFace)
        if (const auto cGoal = AimGoal())
            if (const auto cAt = PlayerAngles())
            {
                const float cMax = std::max(0.05f, m_rate * static_cast<float>(cDelta));
                const int32_t cDx = m_yawAxis.Step((*cGoal)[0], (*cAt)[0], cMax, m_aimTolerance * 0.5f);
                const int32_t cDy = m_pitchAxis.Step((*cGoal)[1], (*cAt)[1], cMax, m_aimTolerance * 0.5f);
                if (cDx || cDy)
                    (void)SendMouseMove(cDx, cDy);
            }
}

namespace
{
// {"cmd": "act", "name", "ready": [...], "turn": {"yaw", "pitch", "face", "rate", "tolerance"}, "buttons": [...],
//  "until": [...], "keep": [...], "watch": [...], "series", "seconds", "ready_seconds", "allow_menus"}: a checked
// action (docs/design/checked-actions.md). Returns its id; "act_state" reads its phase and outcome.
nlohmann::json Act(Harness& aHarness, const nlohmann::json& acRequest)
{
    return aHarness.Actions.Start(aHarness, acRequest);
}

nlohmann::json ActState(Harness& aHarness, const nlohmann::json&)
{
    return aHarness.Actions.State();
}

nlohmann::json ActCancel(Harness& aHarness, const nlohmann::json&)
{
    aHarness.Actions.Cancel(aHarness);
    return aHarness.Actions.State();
}

constexpr Command cCommands[] = {
    {"act", true, &Act},
    {"act_state", false, &ActState},
    {"act_cancel", false, &ActCancel},
};
} // namespace

std::span<const Command> ActionCommands()
{
    return cCommands;
}
} // namespace TestHarness
