#pragma once

#include <DateTime.h>
#include <NoCopyMove.h>

struct World;
struct UpdateEvent;
struct NotifyTimeSettings;
class GuestEngine;

/**
 * @brief A guest's calendar is the host's (HostClock): the game's own clock is off, and this runs the host's
 * on at its speed, taking each jump the host sends. The game's own clock runs again once it is gone.
 */
struct GuestClock
{
    GuestClock(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    ~GuestClock() noexcept;

    NO_COPY_MOVE(GuestClock);

    // False while a guest runs the host's clock: the engine's own (Calendar::Update) then stays off.
    static bool GameClockRuns() noexcept { return !s_hostClock; }

    // Each frame, from GuestService (the parts subscribe to no update: they come and go inside one).
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void OnTimeSettings(const NotifyTimeSettings& acMessage) noexcept;
    // The hour shown while fading from the game's own to the host's.
    float FadedHour() const noexcept;

    static bool s_hostClock;

    World& m_world;
    GuestEngine& m_engine;
    DateTime m_hostTime;
    // The calendar shown when the host's last came in: a fade starts from it.
    TimeModel m_shownTime;
    float m_fadeTimer = 0.f;
    // This game's: the static is the engine hook's (TimeManager.cpp), one per process.
    bool m_hasHostTime = false;
    uint64_t m_lastTick = 0;

    entt::scoped_connection m_timeConnection;
};
