#pragma once

#include <DateTime.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>

struct ServerTimeSettings;
struct NotifyTimeScale;
struct TimeScaleEvent;
struct DisconnectedEvent;
struct World;
struct UpdateEvent;
struct TransportService;

/**
 * @brief Handles time sync: the host's time of day, and one clock speed for everyone (a game slowing
 * its clock slows the others', Services/Host/HostTimeScale.h).
 */
class CalendarService final : public BSTEventSink<TESActivateEvent>
{
public:
    CalendarService(World&, entt::dispatcher&, TransportService&);

    static bool AllowGameTick() noexcept;

private:
    void OnTimeUpdate(const ServerTimeSettings&) noexcept;
    void HandleUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnTimeScale(const TimeScaleEvent&) const noexcept;
    void OnNotifyTimeScale(const NotifyTimeScale&) const noexcept;

    void ToggleGameClock(bool aEnable);
    float TimeInterpolate(const TimeModel& aFrom, TimeModel& aTo) const;

    entt::scoped_connection m_timeUpdateConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_timeScaleConnection;
    entt::scoped_connection m_notifyTimeScaleConnection;

    DateTime m_onlineTime;
    DateTime m_offlineTime;
    float m_fadeTimer = 0.f;
    static bool s_gameClockLocked;

    uint64_t m_lastTick = 0;
    uint64_t m_lastLogTick = 0;
    World& m_world;
    TransportService& m_transport;
};
