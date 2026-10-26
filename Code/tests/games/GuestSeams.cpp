// A guest's side in this process: GuestEngine's calls GuestWorldState makes, on the fake engine's world, and
// GuestService's making of the guest's parts as it joins (its clock, weather, world state and dialogue).
#include "GuestSeams.h"
#include "FakeEngine.h"

#include <Bridges/GuestEngine.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/DialogueService.h>
#include <Services/Guest/GuestClock.h>
#include <Services/Guest/GuestDialogue.h>
#include <Services/Guest/GuestRecord.h>
#include <Services/Guest/GuestPhysics.h>
#include <Services/Guest/GuestWeather.h>
#include <Services/Guest/GuestWorldState.h>
#include <Services/Guest/GuestWait.h>
#include <Services/HostService.h>
#include <World.h>

GuestEngine::GuestEngine() noexcept
{
}

GuestEngine::~GuestEngine() noexcept = default;
// Never read here (TakeOwnMoves is the fake's), but GuestEngine holds one.
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

std::optional<BodyPose> GuestEngine::ReadPose(const BodyKey& acKey) noexcept
{
    const auto& bodies = FakeEngine::Current().Bodies;
    const auto it = acKey.Actor ? bodies.end() : bodies.find(acKey);
    if (it == bodies.end())
        return std::nullopt;
    return BodyPose{.Key = acKey, .Position = it->second.Flight.Body.Position, .Rotation = it->second.Flight.Body.Rotation};
}

bool GuestEngine::DriveBody(const BodyFrame& acFrame, uint32_t) noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    const auto it = engine.Bodies.find(acFrame.Key);
    if (it == engine.Bodies.end())
        return false;
    // A living copy's ragdoll body is its animation's: left until its ragdoll has it (GuestEngine::FindHostBody).
    if (it->second.RagdollOf && !engine.IsActorDead(it->second.RagdollOf))
        return true;
    // Keyframed: put at the frame.
    it->second.Held = true;
    it->second.Flight.Body.Position = acFrame.Position;
    it->second.Flight.Body.Rotation = acFrame.Rotation;
    ++it->second.Drives;
    return true;
}

bool GuestEngine::RestBody(const BodyPose& acPose, uint32_t) noexcept
{
    auto& bodies = FakeEngine::Current().Bodies;
    const auto it = bodies.find(acPose.Key);
    if (it == bodies.end())
        return false;
    FakeEngine::Body& body = it->second;
    // A reference's body is simulated again at its rest; a ragdoll's stays held.
    body.Held = body.RagdollOf != 0;
    body.Flight.Body.Position = acPose.Position;
    body.Flight.Body.Rotation = acPose.Rotation;
    body.Flight.Body.LinearVelocity = {};
    body.Flight.Body.AngularVelocity = {};
    ++body.Rests;
    return true;
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

std::vector<BodyPose> GuestEngine::TakeOwnMoves() noexcept
{
    return std::exchange(FakeEngine::Current().OwnMoves, {});
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
        : Clock(aWorld, aWorld.GetDispatcher(), Engine),
          Weather(aWorld.GetDispatcher(), Engine), WorldState(aWorld, aWorld.GetDispatcher(), Engine),
          Dialogue(aWorld, aWorld.GetDispatcher(), aWorld.ctx().at<DialogueService>()),
          Record(aWorld, aWorld.GetDispatcher(), aWorld.ctx().at<IJoinBridge&>()),
          Physics(aWorld, aWorld.GetDispatcher(), Engine),
          Wait(aWorld, aWorld.GetDispatcher(), Engine)
    {
    }

    GuestEngine Engine;
    GuestClock Clock;
    GuestWeather Weather;
    GuestWorldState WorldState;
    GuestDialogue Dialogue;
    GuestRecord Record;
    GuestPhysics Physics;
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
    m_pGuest->Physics.OnUpdate(cUpdate);
    m_pGuest->Wait.OnUpdate();
}
