#include <Services/Host/HostWait.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>
#include <spdlog/spdlog.h>

namespace
{
// A wait with no readiness or progress for this long ends (ticks are ms).
constexpr uint64_t kStallTicks = 15000;
} // namespace

HostWait::HostWait(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers,
                   const HostGame& acGame) noexcept
    : m_network(aNetwork), m_players(acPlayers), m_game(acGame)
{
    aAuthority.On(*this, &HostWait::OnRequest);
    aAuthority.On(*this, &HostWait::OnWorldRequest);
}

void HostWait::OnRequest(const WaitRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (acRequest.Action == WaitAction::Start)
    {
        if (m_wait)
            return;
        if (!acRequest.Hours || acRequest.Hours > 24)
        {
            NotifyWaitState refused;
            refused.Phase = WaitPhase::Refused;
            m_network.Send(aSender, refused);
            return;
        }
        Wait wait;
        wait.Id = ++m_lastId;
        wait.Hours = acRequest.Hours;
        wait.RemainingSeconds = acRequest.Hours * 3600;
        wait.Requester = aSender;
        wait.Sleeping = acRequest.Sleeping;
        wait.LastActivity = m_tick;
        for (const auto cPlayer : m_players.Connections())
            wait.Pending.insert(cPlayer);
        m_wait = std::move(wait);
        spdlog::info("Shared wait {}: {} asks for {} hours, sleeping {}", m_wait->Id, aSender, m_wait->Hours,
                     m_wait->Sleeping);
        Tell(WaitPhase::Preparing);
        return;
    }
    if (!m_wait || acRequest.Id != m_wait->Id)
        return;

    switch (acRequest.Action)
    {
    case WaitAction::Ready:
        if (!m_wait->Running && m_wait->Pending.erase(aSender) && m_wait->Pending.empty())
        {
            m_wait->Running = true;
            m_wait->LastActivity = m_tick;
            spdlog::info("Shared wait {}: every menu is ready", m_wait->Id);
            Tell(WaitPhase::Running);
        }
        break;
    case WaitAction::Progress:
        if (aSender == m_players.Host() && m_wait->Running && acRequest.RemainingSeconds < m_wait->RemainingSeconds)
        {
            m_wait->RemainingSeconds = acRequest.RemainingSeconds;
            m_wait->LastActivity = m_tick;
            spdlog::info("Shared wait {}: {} seconds remaining", m_wait->Id, m_wait->RemainingSeconds);
            Tell(WaitPhase::Progress);
            if (!m_wait->RemainingSeconds)
                End();
        }
        break;
    case WaitAction::Cancel:
    case WaitAction::Failed:
        spdlog::info("Shared wait {}: {} reports {}", m_wait->Id, aSender, static_cast<uint32_t>(acRequest.Action));
        End();
        break;
    default:
        break;
    }
}

void HostWait::Tell(WaitPhase aPhase) const noexcept
{
    if (!m_wait)
        return;
    NotifyWaitState state;
    state.Phase = aPhase;
    state.Id = m_wait->Id;
    state.Hours = m_wait->Hours;
    state.RemainingSeconds = m_wait->RemainingSeconds;
    state.Calendar = m_game.ReadTime();
    for (const auto cPlayer : m_players.Connections())
    {
        state.Driver = cPlayer == m_players.Host();
        // The host's player is the party's avatar for quest sleep events, whoever used the bed.
        state.Sleeping = m_wait->Sleeping && (state.Driver || cPlayer == m_wait->Requester);
        m_network.Send(cPlayer, state);
    }
}

void HostWait::End() noexcept
{
    if (!m_wait)
        return;
    m_completedId = m_wait->Id;
    Tell(WaitPhase::Finished);
    m_wait.reset();
}

void HostWait::OnWorldRequest(const WaitWorldRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (m_wait || !m_completedId || acRequest.Id != m_completedId || aSender == m_players.Host() || !m_game.ReadCellState)
        return;
    const auto cChanges = m_game.ReadCellState(acRequest.State.Cell, acRequest.State.Changed);
    // An unloaded host cell gets an empty reply. Ordinary cell loading will request its state later.
    const std::vector<ObjectChange> cEmpty;
    const auto& cRows = cChanges ? *cChanges : cEmpty;
    constexpr size_t kRowsPerMessage = 1024;
    for (size_t first = 0; first == 0 || first < cRows.size(); first += kRowsPerMessage)
    {
        NotifyWaitWorld reply;
        reply.Id = acRequest.Id;
        reply.State.Cell = acRequest.State.Cell;
        const size_t cLast = std::min(cRows.size(), first + kRowsPerMessage);
        reply.Last = cLast == cRows.size();
        reply.State.Changes.assign(cRows.begin() + first, cRows.begin() + cLast);
        m_network.Send(aSender, reply);
    }
}

void HostWait::Update(uint64_t aTick) noexcept
{
    m_tick = aTick;
    // A menu that cannot open, or a host countdown interrupted by a load, releases the party.
    if (m_wait && aTick >= m_wait->LastActivity && aTick - m_wait->LastActivity >= kStallTicks)
        End();
}

void HostWait::OnPlayerLeft(ConnectionId_t) noexcept
{
    End();
}

void HostWait::OnPlayerEntered() noexcept
{
    if (m_wait)
        spdlog::info("Shared wait {}: a character arrival ends it", m_wait->Id);
    // A joining game cannot arrive in a world whose countdown is already running.
    End();
}
