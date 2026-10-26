// A guest's side in this process: GuestEngine's calls GuestWorldState makes, on the fake engine's world, and
// GuestService's making of the guest's parts as it joins (its clock, weather, world state and dialogue).
#include "GuestSeams.h"
#include "FakeEngine.h"

#include <Bridges/GuestEngine.h>
#include <Bridges/IBodyBridge.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/DialogueService.h>
#include <Services/Guest/GuestClock.h>
#include <Services/Guest/GuestDialogue.h>
#include <Services/Guest/GuestRecord.h>
#include <Services/Guest/GuestHands.h>
#include <Services/Guest/GuestWeather.h>
#include <Services/Guest/GuestWorldState.h>
#include <Services/Guest/GuestWait.h>
#include <Services/HostService.h>
#include <World.h>

GuestEngine::GuestEngine(IBodyBridge& aBodies) noexcept
    : m_bodyBridge(aBodies)
{
}

GuestEngine::~GuestEngine() noexcept = default;
// Never read here (TakeOwnBodies is the fake's), but GuestEngine holds one.
PhysicsBodies::PhysicsBodies(bool) noexcept {}

void GuestEngine::SetDecidesWorld(bool aOn) noexcept
{
    FakeEngine::Current().DecidesWorld = aOn;
}

void GuestEngine::SetCalendar(const TimeModel& acTime) noexcept
{
    FakeEngine::Current().Calendar = acTime;
}

bool GuestEngine::HoldWeather(const GameId& acWeather) noexcept
{
    FakeEngine::Current().Weather = acWeather;
    FakeEngine::Current().WeatherHeld = true;
    return true;
}

void GuestEngine::ReleaseWeather() noexcept
{
    FakeEngine::Current().WeatherHeld = false;
}

bool GuestEngine::SetGlobal(const GlobalValue& acGlobal) noexcept
{
    FakeEngine::Current().Globals[FormIdOf(acGlobal.Id)] = acGlobal.Value;
    return true;
}

void GuestEngine::ApplyQuest(const QuestState& acQuest, bool) noexcept
{
    FakeEngine::Current().Quests[FormIdOf(acQuest.Id)] = acQuest;
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

void GuestEngine::FreePlaced() noexcept
{
}

bool GuestEngine::IsPlacementPending(const GameId& acObject) const noexcept
{
    return FakeEngine::Current().PendingPlacements.contains(FormIdOf(acObject));
}

std::vector<BodyPush> GuestEngine::TakePushes() noexcept
{
    return std::exchange(FakeEngine::Current().PlayerPushes, {});
}

// The player's ragdoll is keyed by its form id here (0x14), by index alone out of GuestEngine.
GuestEngine::OwnBodies GuestEngine::TakeOwnBodies() noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    OwnBodies own;
    own.Moved = std::exchange(engine.OwnMoves, {});
    auto ragdoll = engine.TakeBodies(World::Get().GetTransport().GetTick(), [](const BodyKey& acKey) { return acKey.Actor == 0x14; });
    for (BodyFrame& frame : ragdoll.Awake)
    {
        frame.Key = {.Index = frame.Key.Index};
        own.Ragdoll.push_back(frame);
    }
    for (BodyPose& rest : ragdoll.Rested)
    {
        rest.Key = {.Index = rest.Key.Index};
        own.RagdollRested.push_back(rest);
    }
    return own;
}

std::vector<BodyFrame> GuestEngine::ReadHeld() const noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    const auto it = engine.Grabbed ? engine.Bodies.find(*engine.Grabbed) : engine.Bodies.end();
    if (it == engine.Bodies.end())
        return {};
    BodyFrame frame = it->second.Flight.Body;
    frame.Key = it->first;
    return {frame};
}

struct GuestSeam::Guest
{
    explicit Guest(World& aWorld)
        : Engine(aWorld.ctx().at<IBodyBridge&>()), Clock(aWorld, aWorld.GetDispatcher(), Engine),
          Weather(aWorld.GetDispatcher(), Engine), WorldState(aWorld, aWorld.GetDispatcher(), Engine),
          Dialogue(aWorld, aWorld.GetDispatcher(), aWorld.ctx().at<DialogueService>()),
          Record(aWorld, aWorld.GetDispatcher(), aWorld.ctx().at<IJoinBridge&>()),
          Hands(aWorld, Engine),
          Wait(aWorld, aWorld.GetDispatcher(), Engine)
    {
    }

    GuestEngine Engine;
    GuestClock Clock;
    GuestWeather Weather;
    GuestWorldState WorldState;
    GuestDialogue Dialogue;
    GuestRecord Record;
    GuestHands Hands;
    GuestWait Wait;
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
    if (!m_pGuest)
        return;
    const UpdateEvent cUpdate(1.0 / 60);
    m_pGuest->Clock.OnUpdate(cUpdate);
    m_pGuest->Weather.OnUpdate(cUpdate);
    m_pGuest->WorldState.OnUpdate(cUpdate);
    m_pGuest->Record.OnUpdate(cUpdate);
    m_pGuest->Engine.FreePlaced();
    m_pGuest->Wait.OnUpdate();
}
