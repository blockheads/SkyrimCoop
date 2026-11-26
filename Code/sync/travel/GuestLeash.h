#pragma once

#include <core/Listen.h>

#include <NoCopyMove.h>

#include <optional>
#include <vector>

struct NotifyLeash;
struct World;
struct UpdateEvent;
class IPlayerBridge;

// The countdown for this game's screen: seconds left, 0 when none runs (World's context; UiService shows its banner,
// the number changing in place: ten HUD notifications ran behind on the duo, party-travel.md).
struct LeashCountdown
{
    int Seconds = 0;
};

/**
 * @brief The soft leash's countdown on a guest (HostLeash; party-travel.md, "Soft leash"): a line counting down each
 * second until the host sends this game's player to it, or the player comes back.
 */
struct GuestLeash
{
    GuestLeash(World& aWorld, entt::dispatcher& aDispatcher, IPlayerBridge& aPlayer) noexcept;
    ~GuestLeash() noexcept;

    NO_COPY_MOVE(GuestLeash);

    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void OnLeash(const NotifyLeash& acMessage) noexcept;

    // The line shown: none without a countdown.
    void Show(int aSeconds) noexcept;

    World& m_world;
    IPlayerBridge& m_player;
    std::optional<double> m_left; // seconds, while counting down
    int m_shown = -1;
    std::vector<entt::scoped_connection> m_connections;
};
