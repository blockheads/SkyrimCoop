#include <Services/Guest/GuestWorldState.h>

#include <Bridges/GuestEngine.h>
#include <Messages/WorldState.h>
#include <Events/UpdateEvent.h>
#include <Events/ContainerChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/LooseItemEvents.h>
#include <Messages/CellStateRequest.h>
#include <Messages/ContainerChangeRequest.h>
#include <Messages/DroppedItems.h>
#include <Messages/LockChangeRequest.h>
#include <Services/TransportService.h>
#include <Services/WorldStateLog.h>
#include <World.h>
#include <FormIds.h>

GuestWorldState::GuestWorldState(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_world(aWorld)
    , m_engine(aEngine)
{
    Listen<NotifyGlobals, &GuestWorldState::OnGlobals>(m_connections, aDispatcher, this);
    Listen<NotifyQuests, &GuestWorldState::OnQuests>(m_connections, aDispatcher, this);
    Listen<NotifyObjectChanges, &GuestWorldState::OnObjectChanges>(m_connections, aDispatcher, this);
    Listen<LockChangeEvent, &GuestWorldState::OnLockChange>(m_connections, aDispatcher, this);
    Listen<ContainerChangeEvent, &GuestWorldState::OnContainerChange>(m_connections, aDispatcher, this);
    Listen<ItemDroppedEvent, &GuestWorldState::OnItemDropped>(m_connections, aDispatcher, this);
    Listen<NotifyItemDropped, &GuestWorldState::OnItemDroppedNotify>(m_connections, aDispatcher, this);
    Listen<ItemPickUpEvent, &GuestWorldState::OnItemPickUp>(m_connections, aDispatcher, this);
    Listen<NotifyPickUp, &GuestWorldState::OnPickUpNotify>(m_connections, aDispatcher, this);
    // this is what stops guests (clients connected to the host) from starting their own quests or running
    // their own world scripts: the host's game decides
    m_engine.SetDecidesWorld(false);
}

GuestWorldState::~GuestWorldState() noexcept
{
    m_engine.SetDecidesWorld(true);
}

void GuestWorldState::OnGlobals(const NotifyGlobals& acMessage) noexcept
{
    size_t missing = 0;
    for (const auto& cValue : acMessage.Values)
        if (!m_engine.SetGlobal(cValue))
            ++missing;
    spdlog::info("GuestWorldState: applied {} of the host's {} globals", acMessage.Values.size() - missing, acMessage.Values.size());
    if (missing)
        spdlog::warn("{}: {} of the host's {} globals are not in this game (or constant here)", __FUNCTION__, missing,
                     acMessage.Values.size());
}

void GuestWorldState::OnQuests(const NotifyQuests& acMessage) noexcept
{
    for (const auto& cQuest : acMessage.Quests)
        m_engine.ApplyQuest(cQuest, acMessage.Join);
    spdlog::info("GuestWorldState: applied the host's {} quests{}", acMessage.Quests.size(), acMessage.Join ? " (as a load)" : "");
}

void GuestWorldState::OnObjectChanges(const NotifyObjectChanges& acMessage) noexcept
{
    // A cell's state arriving after this game left it: the next load asks again.
    if (acMessage.Cell != GameId{} && !m_engine.IsCellAttached(acMessage.Cell))
    {
        spdlog::info("GuestWorldState: the host's state of cell {:X} came after it unloaded here, left", acMessage.Cell.LogFormat());
        return;
    }
    std::vector<ObjectChange> applied;
    for (const auto& cChange : acMessage.Changes)
    {
        if (m_engine.ApplyObjectChange(cChange, acMessage.Cell != GameId{}))
            applied.push_back(cChange);
        else
            spdlog::warn("GuestWorldState: could not apply the host's {}: its object, activator or item is not in this game", WorldStateLog::Describe(cChange));
    }
    if (acMessage.Cell != GameId{})
        spdlog::info("GuestWorldState: applied cell {:X}'s state, {} of {} changes: {}", acMessage.Cell.LogFormat(), applied.size(),
                     acMessage.Changes.size(), WorldStateLog::List(applied, &WorldStateLog::Describe, 16));
    else if (!applied.empty())
        spdlog::info("GuestWorldState: applied {} of the host's object changes: {}", applied.size(), WorldStateLog::List(applied, &WorldStateLog::Describe, 16));
}

void GuestWorldState::AskCellState(uint32_t aCellFormId) noexcept
{
    const auto cRequest = m_engine.ReadCellChanges(aCellFormId);
    if (!cRequest)
        return;
    spdlog::info("GuestWorldState: cell {:X} loaded, asked for the host's state ({} changed here)", cRequest->Cell.LogFormat(), cRequest->Changed.size());
    m_world.GetTransport().Send(*cRequest);
}

void GuestWorldState::OnLockChange(const LockChangeEvent& acEvent) noexcept
{
    // The world's own (its scripts, a cell load) is the host's to decide (step 5).
    if (!acEvent.ByPlayer)
        return;
    const auto cId = ToSession(acEvent.FormId);
    if (!cId)
        return;
    LockChangeRequest request;
    request.Id = *cId;
    request.IsLocked = acEvent.IsLocked;
    request.LockLevel = acEvent.LockLevel;
    spdlog::info("GuestWorldState: proposed lock {:X} {} at {}", request.Id.LogFormat(), request.IsLocked ? "locked" : "unlocked", request.LockLevel);
    m_world.GetTransport().Send(request);
}

void GuestWorldState::OnContainerChange(const ContainerChangeEvent& acEvent) noexcept
{
    // Actors' items are their owner's (InventoryService); a container's are proposed as its count now.
    const auto cChange = m_engine.ReadContainerItems(acEvent.ContainerId, acEvent.ItemId);
    if (!cChange)
        return;
    ContainerChangeRequest request;
    request.Container = cChange->Object;
    request.Item = cChange->Item;
    request.Count = cChange->Value;
    request.Stacks = cChange->Stacks;
    spdlog::info("GuestWorldState: proposed {}", WorldStateLog::Describe(*cChange));
    m_world.GetTransport().Send(request);
}

void GuestWorldState::OnItemDropped(const ItemDroppedEvent& acEvent) noexcept
{
    const auto cRequest = m_engine.ReadDrop(acEvent.ObjectId);
    if (!cRequest)
        return;
    spdlog::info("GuestWorldState: dropped {:X} ({} of {:X}), asked the host to make it", cRequest->Tag, cRequest->Item.Count,
                 cRequest->Item.BaseId.LogFormat());
    m_world.GetTransport().Send(*cRequest);
}

void GuestWorldState::OnItemDroppedNotify(const NotifyItemDropped& acMessage) noexcept
{
    if (acMessage.Object == GameId{})
        spdlog::warn("GuestWorldState: the host's game cannot make drop {:X}: it stays this game's own", acMessage.Tag);
    if (m_engine.AdoptDrop(acMessage.Tag, acMessage.Object))
        spdlog::info("GuestWorldState: the host made drop {:X} as {:X}", acMessage.Tag, acMessage.Object.LogFormat());
    else
        spdlog::warn("GuestWorldState: the host made drop {:X} as {:X}, no longer here", acMessage.Tag, acMessage.Object.LogFormat());
}

void GuestWorldState::OnItemPickUp(const ItemPickUpEvent& acEvent) noexcept
{
    const auto cRequest = m_engine.ReadPickUp(acEvent.ObjectId);
    if (!cRequest)
        return;
    spdlog::info("GuestWorldState: asked the host for {:X} (tag {:X})", cRequest->Object.LogFormat(), cRequest->Tag);
    m_world.GetTransport().Send(*cRequest);
}

void GuestWorldState::OnPickUpNotify(const NotifyPickUp& acMessage) noexcept
{
    m_engine.PickUp(acMessage);
    spdlog::info("GuestWorldState: picked up {:X} (tag {:X}): {} of {:X}", acMessage.Object.LogFormat(), acMessage.Tag, acMessage.Item.Count,
                 acMessage.Item.BaseId.LogFormat());
}

void GuestWorldState::OnUpdate(const UpdateEvent&) noexcept
{
    auto cells = m_engine.AttachedCells();
    for (const uint32_t cCell : cells)
        if (std::ranges::find(m_cells, cCell) == m_cells.end())
            AskCellState(cCell);
    m_cells = std::move(cells);
}
