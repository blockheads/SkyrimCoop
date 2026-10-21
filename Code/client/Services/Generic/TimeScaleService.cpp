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
    m_timeScaleConnection = aDispatcher.sink<TimeScaleEvent>().connect<&TimeScaleService::OnTimeScale>(this);
    m_notifyTimeScaleConnection = aDispatcher.sink<NotifyTimeScale>().connect<&TimeScaleService::OnNotifyTimeScale>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&TimeScaleService::OnDisconnected>(this);
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
