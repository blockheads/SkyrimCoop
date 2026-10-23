#include <Services/TimeScaleService.h>

#include <Bridges/IWorldBridge.h>
#include <Events/DisconnectedEvent.h>
#include <Events/TimeScaleEvent.h>
#include <Messages/NotifyTimeScale.h>
#include <Messages/TimeScaleRequest.h>
#include <Services/TransportService.h>
#include <World.h>

TimeScaleService::TimeScaleService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    Listen<TimeScaleEvent, &TimeScaleService::OnTimeScale>(m_connections, aDispatcher, this);
    Listen<NotifyTimeScale, &TimeScaleService::OnNotifyTimeScale>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &TimeScaleService::OnDisconnected>(m_connections, aDispatcher, this);
}

void TimeScaleService::OnTimeScale(const TimeScaleEvent& acEvent) const noexcept
{
    TimeScaleRequest request;
    request.Multiplier = acEvent.Multiplier;
    m_transport.Send(request);
}

void TimeScaleService::OnNotifyTimeScale(const NotifyTimeScale& acMessage) const noexcept
{
    m_world.ctx().at<IWorldBridge&>().SetSharedTimeScale(acMessage.Multiplier);
}

void TimeScaleService::OnDisconnected(const DisconnectedEvent&) const noexcept
{
    m_world.ctx().at<IWorldBridge&>().SetSharedTimeScale(1.f);
}
