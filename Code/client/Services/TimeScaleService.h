#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

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

    NO_COPY_MOVE(TimeScaleService);

private:
    void OnTimeScale(const TimeScaleEvent& acEvent) const noexcept;
    void OnNotifyTimeScale(const NotifyTimeScale& acMessage) const noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) const noexcept;

    World& m_world;
    TransportService& m_transport;

    std::vector<entt::scoped_connection> m_connections;
};
