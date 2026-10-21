#pragma once

#include <DateTime.h>

struct World;
struct UpdateEvent;
struct ServerTimeSettings;
class GuestEngine;

/**
 * @brief A guest's calendar is the host's (HostClock): the game's own clock is off, and this runs the host's
 * on at its speed, taking each jump the host sends. The game's own clock runs again once it is gone.
 */
struct GuestClock
{
    GuestClock(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    ~GuestClock() noexcept;

    TP_NOCOPYMOVE(GuestClock);

    // False while a guest runs the host's clock: the engine's own (Calendar::Update) then stays off.
    static bool GameClockRuns() noexcept { return !s_hostClock; }

private:
    void OnTimeSettings(const ServerTimeSettings& acMessage) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    // The hour shown while fading from the game's own to the host's.
    float FadedHour() const noexcept;

    static bool s_hostClock;

    World& m_world;
    GuestEngine& m_engine;
    DateTime m_hostTime;
    // The calendar shown when the host's last came in: a fade starts from it.
    TimeModel m_shownTime;
    float m_fadeTimer = 0.f;
    uint64_t m_lastTick = 0;

    entt::scoped_connection m_timeConnection;
    entt::scoped_connection m_updateConnection;
};
