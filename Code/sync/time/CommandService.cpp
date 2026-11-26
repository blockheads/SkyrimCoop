
#include <time/CommandService.h>
#include <core/TransportService.h>

#include <core/World.h>


#include <time/SetTimeCommandEvent.h>

#include "Messages/SetTimeCommandRequest.h"


CommandService::CommandService(TransportService& aTransport, entt::dispatcher& aDispatcher)
    : m_transport(aTransport)
{
    Listen<SetTimeCommandEvent, &CommandService::OnSetTimeCommand>(m_connections, aDispatcher, this);
}

void CommandService::OnSetTimeCommand(const SetTimeCommandEvent& acEvent) const noexcept
{
    SetTimeCommandRequest request{};
    request.Hours = acEvent.Hours;
    request.Minutes = acEvent.Minutes;
    request.PlayerId = acEvent.PlayerId;
    m_transport.Send(request);
}

