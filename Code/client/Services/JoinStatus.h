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
        // Joining from the main menu (JoinService):
        kChecking,
        kReceivingWorld,
        kLoading,
        kCreatingCharacter,
        // A guest's session dropped: trying the same host again, then ended (or the host removed it).
        kReconnecting,
        kEnded,
    };

    State Current = State::kIdle;
    // When refused or ended, a stable code: wrong_build, wrong_game, full, mods_differ, unreachable, lost_connection,
    // transfer_failed, placement_failed, kicked.
    std::string Reason;
    // When refused, one sentence the player can act on, and a line per difference.
    std::string Message;
    std::vector<std::string> Details;
    // Receiving the world: how much of the host's save is here (0 to 1).
    float Progress{};

    bool operator==(const JoinStatus&) const = default;
};

[[nodiscard]] constexpr const char* ToString(JoinStatus::State aState) noexcept
{
    switch (aState)
    {
    case JoinStatus::State::kConnecting: return "connecting";
    case JoinStatus::State::kJoined: return "joined";
    case JoinStatus::State::kRefused: return "refused";
    case JoinStatus::State::kChecking: return "checking";
    case JoinStatus::State::kReceivingWorld: return "receiving_world";
    case JoinStatus::State::kLoading: return "loading";
    case JoinStatus::State::kCreatingCharacter: return "creating_character";
    case JoinStatus::State::kReconnecting: return "reconnecting";
    case JoinStatus::State::kEnded: return "ended";
    default: return "idle";
    }
}
