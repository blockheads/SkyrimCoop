#include <Services/WaitService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/GameLoadedEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/TransportService.h>
#include <World.h>

namespace
{
// A request the host never answers gives the menu back (ticks are ms).
constexpr uint64_t kAnswerTicks = 15000;
} // namespace

WaitService::WaitService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                         IWaitBridge& aBridge) noexcept
    : m_world(aWorld), m_transport(aTransport), m_bridge(aBridge)
{
    Listen<NotifyWaitState, &WaitService::OnState>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &WaitService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &WaitService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<GameLoadedEvent, &WaitService::OnLoaded>(m_connections, aDispatcher, this);
    m_bridge.SetCallbacks({
        .Request = [this](uint32_t aHours, bool aSleeping) { return Request(aHours, aSleeping); },
        .Cancel = [this] { return Cancel(); },
        .Stepped = [this](uint32_t aRemainingSeconds) { Stepped(aRemainingSeconds); },
    });
}

WaitService::~WaitService() noexcept
{
    m_bridge.SetCallbacks({});
    Reset();
}

void WaitService::Send(WaitAction aAction, uint32_t aValue, bool aSleeping) noexcept
{
    WaitRequest request;
    request.Action = aAction;
    request.Id = m_id;
    request.Hours = aAction == WaitAction::Start ? aValue : 0;
    request.RemainingSeconds = aAction == WaitAction::Progress ? aValue : 0;
    request.Sleeping = aSleeping;
    m_transport.Send(request);
}

bool WaitService::Request(uint32_t aHours, bool aSleeping) noexcept
{
    if (!m_transport.IsOnline())
        return false;
    if (!m_pending && !m_id)
    {
        m_pending = true;
        m_askedAt = m_world.GetTick();
        Send(WaitAction::Start, aHours, aSleeping);
    }
    return true;
}

bool WaitService::Cancel() noexcept
{
    if (m_pending)
    {
        m_cancelPending = true;
        return true;
    }
    if (!m_id)
        return false;
    Send(WaitAction::Cancel);
    return true;
}

void WaitService::Stepped(uint32_t aRemainingSeconds) noexcept
{
    if (m_id && m_running && m_driver && aRemainingSeconds < m_remainingSeconds)
    {
        m_remainingSeconds = aRemainingSeconds;
        Send(WaitAction::Progress, aRemainingSeconds);
    }
}

void WaitService::Reset() noexcept
{
    m_id = m_remainingSeconds = 0;
    m_steps.clear();
    m_pending = m_cancelPending = m_ready = m_running = m_driver = m_finished = false;
    m_bridge.End();
}

void WaitService::OnState(const NotifyWaitState& acMessage) noexcept
{
    if (acMessage.Phase == WaitPhase::Refused)
    {
        if (m_pending && !m_id)
            Reset();
        return;
    }
    if (acMessage.Phase == WaitPhase::Preparing)
    {
        if (!acMessage.Id || acMessage.Id == m_id)
            return;
        m_id = acMessage.Id;
        m_pending = false;
        m_ready = m_running = m_finished = false;
        m_driver = acMessage.Driver;
        m_remainingSeconds = acMessage.RemainingSeconds;
        m_steps.clear();
        if (std::exchange(m_cancelPending, false))
            Send(WaitAction::Cancel);
        else if (!m_bridge.Prepare(acMessage.Hours, acMessage.Sleeping, m_driver))
            Send(WaitAction::Failed);
        return;
    }
    if (!m_id || acMessage.Id != m_id)
        return;

    switch (acMessage.Phase)
    {
    case WaitPhase::Running:
        if (!m_running)
        {
            m_running = true;
            m_bridge.Begin();
        }
        break;
    case WaitPhase::Progress:
        if (!m_driver && m_running && acMessage.RemainingSeconds < m_remainingSeconds)
        {
            m_steps.push_back(acMessage.RemainingSeconds);
            m_remainingSeconds = acMessage.RemainingSeconds;
        }
        break;
    case WaitPhase::Finished:
        // Reliable progress comes first: apply the final native step before closing the menu.
        m_finished = true;
        break;
    default:
        break;
    }
}

void WaitService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_id && !m_ready && m_bridge.IsReady())
    {
        m_ready = true;
        Send(WaitAction::Ready);
    }
    for (const auto cRemaining : std::exchange(m_steps, {}))
        m_bridge.Step(cRemaining);
    if (m_finished || (m_pending && m_world.GetTick() - m_askedAt >= kAnswerTicks))
        Reset();
}

void WaitService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    Reset();
}

void WaitService::OnLoaded(const GameLoadedEvent&) noexcept
{
    if (m_id)
        Send(WaitAction::Cancel);
    Reset();
}
