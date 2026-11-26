#pragma once

#include <Messages/NotifySetWaypoint.h>
#include <NoCopyMove.h>
#include <network/ENetInterface.h>

#include <optional>

struct HostNetwork;
struct HostRouter;
struct RequestRemoveWaypoint;
struct RequestSetWaypoint;

// The party's map marker: every game shows the last one set, kept for a joiner.
struct HostMap
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aRouter.
    HostMap(HostRouter& aRouter, HostNetwork& aNetwork) noexcept;
    NO_COPY_MOVE(HostMap);

    // A joiner gets the marker, if one is set.
    void SendWaypoint(ConnectionId_t aPlayer) const noexcept;

  private:
    void OnSetWaypoint(const RequestSetWaypoint& acRequest, ConnectionId_t aSender) noexcept;
    void OnRemoveWaypoint(const RequestRemoveWaypoint& acRequest, ConnectionId_t aSender) noexcept;

    const HostRouter& m_router;
    HostNetwork& m_network;
    std::optional<NotifySetWaypoint> m_waypoint;
};
