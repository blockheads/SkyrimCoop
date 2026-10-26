#include <Services/Guest/GuestWait.h>

#include <Bridges/GuestEngine.h>
#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Services/TransportService.h>
#include <World.h>

GuestWait::GuestWait(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_world(aWorld), m_engine(aEngine)
{
    Listen<NotifyWaitState, &GuestWait::OnState>(m_connections, aDispatcher, this);
    Listen<MenuEvent, &GuestWait::OnMenu>(m_connections, aDispatcher, this);
    Listen<NotifyWaitWorld, &GuestWait::OnWorld>(m_connections, aDispatcher, this);
    Listen<NotifyObjectChanges, &GuestWait::OnChanges>(m_connections, aDispatcher, this);
    Listen<GameLoadedEvent, &GuestWait::OnLoaded>(m_connections, aDispatcher, this);
}

void GuestWait::OnState(const NotifyWaitState& acMessage) noexcept
{
    if (acMessage.Phase == WaitPhase::Preparing && acMessage.Id)
    {
        m_id = acMessage.Id;
        m_finished = false;
        m_cells.clear();
        m_records.clear();
    }
    else if (acMessage.Phase == WaitPhase::Finished && m_id && acMessage.Id == m_id)
        m_finished = true;
}

void GuestWait::OnMenu(const MenuEvent& acEvent) noexcept
{
    if (acEvent.Menu != "Sleep/Wait Menu" || acEvent.Opening || !std::exchange(m_finished, false))
        return;
    // Fast-forward may reset local rows without unloading their cell. Restore the host's state.
    for (const auto cCell : m_engine.AttachedCells())
    {
        const auto cState = m_engine.ReadCellChanges(cCell);
        if (!cState)
            continue;
        WaitWorldRequest request;
        request.Id = m_id;
        request.State = *cState;
        m_cells.push_back(cState->Cell);
        m_world.GetTransport().Send(request);
    }
}

void GuestWait::OnWorld(const NotifyWaitWorld& acMessage) noexcept
{
    const auto cCell = acMessage.State.Cell;
    if (!m_id || acMessage.Id != m_id || std::ranges::find(m_cells, cCell) == m_cells.end())
        return;
    if (m_engine.IsCellAttached(cCell))
        for (const auto& cChange : acMessage.State.Changes)
        {
            if (cChange.Type == ObjectChange::Kind::kRecord)
            {
                std::erase_if(m_records, [&](const Record& acRecord) { return acRecord.Change.Object == cChange.Object; });
                m_records.push_back({cCell, cChange});
            }
            else
                m_engine.ApplyObjectChange(cChange, true);
        }
    if (acMessage.Last)
        std::erase(m_cells, cCell);
}

void GuestWait::OnChanges(const NotifyObjectChanges& acMessage) noexcept
{
    // A newer ordinary update supersedes any saved flags still waiting from this rest's snapshot.
    for (const auto& cChange : acMessage.Changes)
        std::erase_if(m_records, [&](const Record& acRecord) { return acRecord.Change.Object == cChange.Object; });
}

void GuestWait::OnUpdate() noexcept
{
    std::erase_if(m_records, [this](const Record& acRecord) {
        if (!m_engine.IsCellAttached(acRecord.Cell))
            return true;
        if (std::ranges::find(m_cells, acRecord.Cell) != m_cells.end() || m_engine.IsPlacementPending(acRecord.Change.Object))
            return false;
        m_engine.ApplyObjectChange(acRecord.Change, true);
        return true;
    });
}

void GuestWait::OnLoaded(const GameLoadedEvent&) noexcept
{
    m_id = 0;
    m_finished = false;
    m_cells.clear();
    m_records.clear();
}
