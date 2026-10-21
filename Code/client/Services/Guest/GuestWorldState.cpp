#include <Services/Guest/GuestWorldState.h>

#include <Bridges/GuestEngine.h>
#include <Messages/WorldState.h>
#include <Events/LockChangeEvent.h>
#include <Messages/LockChangeRequest.h>
#include <Services/TransportService.h>
#include <World.h>

GuestWorldState::GuestWorldState(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_world(aWorld)
    , m_engine(aEngine)
{
    m_globalsConnection = aDispatcher.sink<NotifyGlobals>().connect<&GuestWorldState::OnGlobals>(this);
    m_questsConnection = aDispatcher.sink<NotifyQuests>().connect<&GuestWorldState::OnQuests>(this);
    m_objectChangesConnection = aDispatcher.sink<NotifyObjectChanges>().connect<&GuestWorldState::OnObjectChanges>(this);
    m_lockChangeConnection = aDispatcher.sink<LockChangeEvent>().connect<&GuestWorldState::OnLockChange>(this);
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
    if (missing)
        spdlog::warn("{}: {} of the host's {} globals are not in this game (or constant here)", __FUNCTION__, missing,
                     acMessage.Values.size());
}

void GuestWorldState::OnQuests(const NotifyQuests& acMessage) noexcept
{
    for (const auto& cQuest : acMessage.Quests)
        m_engine.ApplyQuest(cQuest, acMessage.Join);
}

void GuestWorldState::OnObjectChanges(const NotifyObjectChanges& acMessage) noexcept
{
    for (const auto& cChange : acMessage.Changes)
        if (!m_engine.ApplyObjectChange(cChange))
            spdlog::debug("{}: object {:X} of a host object change is not in this game", __FUNCTION__, cChange.Object.LogFormat());
}

void GuestWorldState::OnLockChange(const LockChangeEvent& acEvent) noexcept
{
    LockChangeRequest request;
    if (!m_world.GetModSystem().GetServerModId(acEvent.FormId, request.Id))
        return;
    request.IsLocked = acEvent.IsLocked;
    request.LockLevel = acEvent.LockLevel;
    m_world.GetTransport().Send(request);
}
