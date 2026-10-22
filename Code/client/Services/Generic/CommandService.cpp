
#include <Services/CommandService.h>
#include <Services/TransportService.h>

#include <World.h>


#include <Events/SetTimeCommandEvent.h>

#include "Messages/SetTimeCommandRequest.h"


CommandService::CommandService(TransportService& aTransport, entt::dispatcher& aDispatcher)
    : m_transport(aTransport)
{
    m_setTimeConnection = aDispatcher.sink<SetTimeCommandEvent>().connect<&CommandService::OnSetTimeCommand>(this);
}

void CommandService::OnSetTimeCommand(const SetTimeCommandEvent& acEvent) const noexcept
{
    SetTimeCommandRequest request{};
    request.Hours = acEvent.Hours;
    request.Minutes = acEvent.Minutes;
    request.PlayerId = acEvent.PlayerId;
    m_transport.Send(request);
}

