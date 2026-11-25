#include <Services/Host/HostMap.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostRouter.h>

#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/RequestRemoveWaypoint.h>
#include <Messages/RequestSetWaypoint.h>

HostMap::HostMap(HostRouter& aRouter, HostNetwork& aNetwork) noexcept
    : m_router(aRouter)
    , m_network(aNetwork)
{
    aRouter.On(*this, &HostMap::OnSetWaypoint);
    aRouter.On(*this, &HostMap::OnRemoveWaypoint);
}

void HostMap::SendWaypoint(ConnectionId_t aPlayer) const noexcept
{
    if (m_waypoint)
        m_network.Send(aPlayer, *m_waypoint);
}

void HostMap::OnSetWaypoint(const RequestSetWaypoint& acRequest, ConnectionId_t aSender) noexcept
{
    NotifySetWaypoint notify;
    notify.Position = acRequest.Position;
    notify.WorldSpaceFormID = acRequest.WorldSpaceFormID;
    m_waypoint = notify;
    m_router.Deliver(notify, HostRouter::Delivery::FromAnyone, 0, aSender);
}

void HostMap::OnRemoveWaypoint(const RequestRemoveWaypoint&, ConnectionId_t aSender) noexcept
{
    m_waypoint.reset();
    m_router.Deliver(NotifyRemoveWaypoint{}, HostRouter::Delivery::FromAnyone, 0, aSender);
}
