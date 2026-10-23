// A guest's side in this process: GuestEngine's calls GuestWorldState makes, on the fake engine's world, and
// GuestService's making of the guest's parts as it joins (only GuestWorldState so far).
#include "GuestSeams.h"
#include "FakeEngine.h"

#include <Bridges/GuestEngine.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/Guest/GuestWorldState.h>
#include <Services/HostService.h>
#include <World.h>

GuestEngine::GuestEngine(const ModSystem& acMods) noexcept : m_mods(acMods)
{
}

GuestEngine::~GuestEngine() noexcept = default;

void GuestEngine::SetDecidesWorld(bool aOn) noexcept
{
    FakeEngine::Current().DecidesWorld = aOn;
}

bool GuestEngine::SetGlobal(const GlobalValue&) noexcept
{
    return true;
}

void GuestEngine::ApplyQuest(const QuestState&, bool) noexcept
{
}

bool GuestEngine::ApplyObjectChange(const ObjectChange& acChange, bool aLoad) noexcept
{
    return FakeEngine::Current().Apply(acChange, aLoad);
}

std::optional<ObjectChange> GuestEngine::ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) const noexcept
{
    const auto& cReferences = FakeEngine::Current().References;
    const auto it = cReferences.find(aObjectFormId);
    if (it == cReferences.end())
        return std::nullopt;
    const auto cItem = it->second.Items.find(aItemFormId);
    ObjectChange change{
        .Type = ObjectChange::Kind::kItems, .Object = SessionIdOf(aObjectFormId), .Item = SessionIdOf(aItemFormId)};
    change.Value = cItem != it->second.Items.end() ? static_cast<uint32_t>(cItem->second) : 0;
    return change;
}

std::vector<uint32_t> GuestEngine::AttachedCells() const noexcept
{
    const auto& cAttached = FakeEngine::Current().Attached;
    return {cAttached.begin(), cAttached.end()};
}

std::optional<CellStateRequest> GuestEngine::ReadCellChanges(uint32_t aCellFormId) const noexcept
{
    return FakeEngine::Current().CellChanges(aCellFormId);
}

bool GuestEngine::IsCellAttached(const GameId& acCell) const noexcept
{
    return FakeEngine::Current().Attached.contains(FormIdOf(acCell));
}

struct GuestSeam::Guest
{
    explicit Guest(World& aWorld) : Engine(aWorld.GetModSystem()), WorldState(aWorld, aWorld.GetDispatcher(), Engine)
    {
    }

    GuestEngine Engine;
    GuestWorldState WorldState;
};

GuestSeam::GuestSeam(World& aWorld) noexcept : m_world(aWorld)
{
    auto& dispatcher = aWorld.GetDispatcher();
    m_connected = dispatcher.sink<ConnectedEvent>().connect<&GuestSeam::OnConnected>(this);
    m_disconnected = dispatcher.sink<DisconnectedEvent>().connect<&GuestSeam::OnDisconnected>(this);
}

GuestSeam::~GuestSeam() noexcept = default;

void GuestSeam::OnConnected(const ConnectedEvent&) noexcept
{
    // The host joins its own session too: its world is its own (GuestService::OnConnected).
    if (!m_world.GetHostService().IsHosting())
        m_pGuest = std::make_unique<Guest>(m_world);
}

void GuestSeam::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_pGuest.reset();
}

void GuestSeam::Update() noexcept
{
    if (m_pGuest)
        m_pGuest->WorldState.OnUpdate(UpdateEvent(1.0 / 60));
}
