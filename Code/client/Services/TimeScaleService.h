#pragma once

struct NotifyTimeScale;
struct TimeScaleEvent;
struct DisconnectedEvent;
struct World;
struct TransportService;

/**
 * @brief One clock speed for everyone (HostTimeScale): a game slowing its clock (a kill cam, Slow Time) slows
 * the others'. Every game, host and guests.
 */
struct TimeScaleService
{
    TimeScaleService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;

    TP_NOCOPYMOVE(TimeScaleService);

private:
    void OnTimeScale(const TimeScaleEvent& acEvent) const noexcept;
    void OnNotifyTimeScale(const NotifyTimeScale& acMessage) const noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) const noexcept;

    World& m_world;
    TransportService& m_transport;

    entt::scoped_connection m_timeScaleConnection;
    entt::scoped_connection m_notifyTimeScaleConnection;
    entt::scoped_connection m_disconnectedConnection;
};
