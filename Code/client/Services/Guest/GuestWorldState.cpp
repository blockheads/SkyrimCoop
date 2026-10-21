#include <Services/Guest/GuestWorldState.h>

#include <Bridges/GuestEngine.h>
#include <Messages/WorldState.h>
#include <Events/ContainerChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Messages/ContainerChangeRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Services/TransportService.h>
#include <Services/WorldStateLog.h>
#include <World.h>

GuestWorldState::GuestWorldState(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_world(aWorld)
    , m_engine(aEngine)
{
    m_globalsConnection = aDispatcher.sink<NotifyGlobals>().connect<&GuestWorldState::OnGlobals>(this);
    m_questsConnection = aDispatcher.sink<NotifyQuests>().connect<&GuestWorldState::OnQuests>(this);
    m_objectChangesConnection = aDispatcher.sink<NotifyObjectChanges>().connect<&GuestWorldState::OnObjectChanges>(this);
    m_lockChangeConnection = aDispatcher.sink<LockChangeEvent>().connect<&GuestWorldState::OnLockChange>(this);
    m_containerChangeConnection = aDispatcher.sink<ContainerChangeEvent>().connect<&GuestWorldState::OnContainerChange>(this);
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
    TiltedPhoques::Vector<ObjectChange> applied;
    for (const auto& cChange : acMessage.Changes)
    {
        if (m_engine.ApplyObjectChange(cChange))
            applied.push_back(cChange);
        else
            spdlog::warn("GuestWorldState: could not apply the host's {}: its object, activator or item is not in this game", WorldStateLog::Describe(cChange));
    }
    if (!applied.empty())
        spdlog::info("GuestWorldState: applied {} of the host's object changes: {}", applied.size(), WorldStateLog::List(applied, &WorldStateLog::Describe, 16));
}

void GuestWorldState::OnLockChange(const LockChangeEvent& acEvent) noexcept
{
    // The world's own (its scripts, a cell load) is the host's to decide (step 5).
    if (!acEvent.ByPlayer)
        return;
    LockChangeRequest request;
    if (!m_world.GetModSystem().GetServerModId(acEvent.FormId, request.Id))
        return;
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
    spdlog::info("GuestWorldState: proposed {}", WorldStateLog::Describe(*cChange));
    m_world.GetTransport().Send(request);
}
