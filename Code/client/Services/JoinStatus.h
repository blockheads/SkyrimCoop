#pragma once

#include <string>
#include <vector>

/**
 * @brief This game's attempt to join a session, for whatever shows it: the join panel, the test
 * harness, later the status file (docs/design/join-flow.md, "Errors").
 */
struct JoinStatus
{
    enum class State
    {
        kIdle,
        kConnecting,
        kJoined,
        kRefused,
    };

    State Current = State::kIdle;
    // When refused, a stable code: wrong_build, full, mods_differ, refused.
    std::string Reason;
    // When refused, one sentence the player can act on, and a line per difference.
    std::string Message;
    std::vector<std::string> Details;

    bool operator==(const JoinStatus&) const = default;
};

[[nodiscard]] constexpr const char* ToString(JoinStatus::State aState) noexcept
{
    switch (aState)
    {
    case JoinStatus::State::kConnecting: return "connecting";
    case JoinStatus::State::kJoined: return "joined";
    case JoinStatus::State::kRefused: return "refused";
    default: return "idle";
    }
}
