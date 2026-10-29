// A guest's side in this process: GuestEngine's calls GuestWorldState makes, on the fake engine's world, and
// GuestService's making of the guest's parts as it joins (its clock, weather, world state and dialogue).
#include "GuestSeams.h"
#include "FakeEngine.h"

#include <Bridges/GuestEngine.h>
#include <Bridges/IBodyBridge.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/StoryEvents.h>
#include <FormIds.h>
#include <Events/InventoryChangeEvent.h>
#include <Games/Overrides.h>
#include <Services/DialogueService.h>
#include <Services/Guest/GuestClock.h>
#include <Services/Guest/GuestDialogue.h>
#include <Services/Guest/GuestRecord.h>
#include <Services/Guest/GuestScene.h>
#include <Services/Guest/GuestHands.h>
#include <Services/Guest/GuestWeather.h>
#include <Services/Guest/GuestWorldState.h>
#include <Services/Guest/GuestWait.h>
#include <Services/HostService.h>
#include <Messages/Relayed.h>
#include <ScriptValues.h>
#include <World.h>
#include <ScriptValues.h>
#include <Messages/Relayed.h>
#include <SessionIds.h>

GuestEngine::GuestEngine(IBodyBridge& aBodies) noexcept
    : m_bodyBridge(aBodies)
{
}

GuestEngine::~GuestEngine() noexcept = default;

bool GuestEngine::RunScenePackage(const GameId& acPackage) noexcept
{
    FakeEngine::Current().AiDriven = true;
    FakeEngine::Current().ScenePackage = FormIdOf(acPackage);
    return true;
}

void GuestEngine::EndScenePackage() noexcept
{
    FakeEngine::Current().AiDriven = false;
}
// Never read here (TakeOwnBodies is the fake's), but GuestEngine holds one.
PhysicsBodies::PhysicsBodies(bool) noexcept {}

void GuestEngine::SetDecidesWorld(bool aOn) noexcept
{
    FakeEngine::Current().DecidesWorld = aOn;
}

// As StoryEvents::ToSend: the fake's members name forms by their ids.
std::optional<StoryEvent> GuestEngine::ReadStoryEvent(const OwnStoryEvent& acEvent) const noexcept
{
    const FakeEngine& cEngine = FakeEngine::Current();
    if (acEvent.Index >= cEngine.StoryTypes.size())
        return std::nullopt;
    using StoryEventRules::Named;
    const auto cReference = [&cEngine](uint32_t aId) -> Named {
        if (!aId)
            return {};
        if (aId == FakeEngine::kPlayer)
            return {Named::kThisPlayer};
        if (cEngine.Actors.contains(aId))
            return {Named::kActor};
        const auto cId = ToSession(aId);
        return cId ? Named{Named::kObject, *cId} : Named{};
    };
    const auto cForm = [](uint64_t aId) { return ToSession(static_cast<uint32_t>(aId)); };
    const auto& cType = cEngine.StoryTypes[acEvent.Index];
    auto members = StoryEventRules::ToSend(cType.Members, acEvent.Members, cReference, cForm);
    if (!members)
        return std::nullopt;
    return StoryEvent{.Type = cType.Id, .Members = std::move(*members)};
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
    FakeEngine& engine = FakeEngine::Current();
    if (engine.SystemQuestList.IsGlobal(FormIdOf(acGlobal.Id)))
        return false; // as GuestEngine: this player's own
    engine.Globals[FormIdOf(acGlobal.Id)] = acGlobal.Value;
    return true;
}

void GuestEngine::ApplyQuest(const QuestState& acQuest, bool aAsLoad) noexcept
{
    auto& engine = FakeEngine::Current();
    const uint32_t cId = FormIdOf(acQuest.Id);
    if (engine.SystemQuestList.Contains(cId))
        return;
    engine.Quests[cId] = acQuest;
    m_pendingAliases[cId] = acQuest;
    RetryQuestAliases();
}

void GuestEngine::RetryQuestAliases() noexcept
{
    auto& engine = FakeEngine::Current();
    for (auto it = m_pendingAliases.begin(); it != m_pendingAliases.end();)
    {
        bool ready = true;
        auto& aliases = engine.QuestAliasReferences[it->first];
        if (!(it->second.Flags & 0x81))
            aliases.clear();
        else
            for (const auto& cFill : it->second.Aliases)
            {
                if (cFill.HostOnly)
                    continue;
                const uint32_t cRef = cFill.ActorId ? Utils::GetFormIdBySessionId(cFill.ActorId) : FormIdOf(cFill.Ref);
                if (cFill.ActorId && !cRef)
                {
                    ready = false;
                    continue;
                }
                aliases[cFill.AliasId] = cRef;
            }
        it = ready ? m_pendingAliases.erase(it) : std::next(it);
    }
}

bool GuestEngine::ApplyObjectChange(const ObjectChange& acChange, bool aLoad) noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    if (acChange.Object.ModId != ObjectChange::kCreatedModId)
        return engine.Apply(acChange, aLoad);
    // The host's loose items: a twin made once (Create), deleted with the host's.
    const auto cTwin = m_created.find(acChange.Object.BaseId);
    const bool cHere = cTwin != m_created.end() && engine.Loose.contains(cTwin->second);
    if (acChange.Type == ObjectChange::Kind::kCreate)
    {
        if (cHere)
            return true;
        ObjectChange::Placement placement;
        if (!ObjectChange::UnpackPlacement(acChange.Data, placement))
            return false;
        Inventory::Entry item = acChange.Stacks.empty() ? Inventory::Entry{.BaseId = acChange.Item, .Count = 1} : acChange.Stacks[0];
        const uint32_t cId = engine.PlaceLoose(FormIdOf(placement.Cell), item);
        m_created[acChange.Object.BaseId] = cId;
        engine.HostDecides.insert(cId);
        return true;
    }
    if (!cHere)
        return false;
    if (acChange.Type == ObjectChange::Kind::kEnabled && acChange.Value & 2)
    {
        engine.Loose.erase(cTwin->second);
        engine.HostDecides.erase(cTwin->second);
        m_created.erase(cTwin);
    }
    return true;
}

std::optional<DropItemRequest> GuestEngine::ReadDrop(uint32_t aFormId) noexcept
{
    const auto& cLoose = FakeEngine::Current().Loose;
    const auto it = cLoose.find(aFormId);
    if (it == cLoose.end())
        return std::nullopt;
    m_ownDrops[aFormId] = aFormId;
    FakeEngine::Current().HostDecides.insert(aFormId);
    DropItemRequest drop;
    drop.Tag = aFormId;
    drop.Item = it->second.Item;
    drop.Placement.Cell = SessionIdOf(it->second.Cell);
    return drop;
}

bool GuestEngine::AdoptDrop(uint32_t aTag, const GameId& acObject) noexcept
{
    const auto it = m_ownDrops.find(aTag);
    if (it == m_ownDrops.end() || !FakeEngine::Current().Loose.contains(it->second))
        return false;
    const uint32_t cOwn = it->second;
    m_ownDrops.erase(it);
    if (acObject == GameId{})
    {
        FakeEngine::Current().HostDecides.erase(cOwn);
        return true;
    }
    m_created[acObject.BaseId] = cOwn;
    m_adopted.push_back({.Object = acObject});
    return true;
}

std::vector<BodyKey> GuestEngine::TakeAdoptedDrops() noexcept
{
    return std::exchange(m_adopted, {});
}

std::optional<PickUpRequest> GuestEngine::ReadPickUp(uint32_t aFormId) const noexcept
{
    PickUpRequest request;
    for (const auto& [cHostId, cOwn] : m_created)
        if (cOwn == aFormId)
            request.Object = GameId(ObjectChange::kCreatedModId, cHostId);
    if (m_ownDrops.contains(aFormId))
        request.Tag = aFormId;
    if (request.Object == GameId{} && !request.Tag)
        return std::nullopt;
    return request;
}

void GuestEngine::PickUp(const NotifyPickUp& acGrant) noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    uint32_t twin = 0;
    if (const auto it = m_created.find(acGrant.Object.BaseId); acGrant.Object.ModId == ObjectChange::kCreatedModId && it != m_created.end())
    {
        twin = it->second;
        m_created.erase(it);
    }
    if (const auto it = m_ownDrops.find(acGrant.Tag); acGrant.Tag && it != m_ownDrops.end())
    {
        twin = it->second;
        m_ownDrops.erase(it);
    }
    if (twin && engine.Loose.contains(twin))
    {
        engine.PickUpGranted = true;
        engine.PicksUp(twin);
        engine.PickUpGranted = false;
        return;
    }
    // Its twin gone already: the item itself, raised as a pickup is.
    World::Get().GetRunner().Trigger(InventoryChangeEvent(FakeEngine::kPlayer, acGrant.Item));
    ScopedInventoryOverride _;
    engine.ObjectAddOrRemoveItem(FakeEngine::kPlayer, acGrant.Item);
}

std::optional<ObjectChange> GuestEngine::ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) const noexcept
{
    const auto& cReferences = FakeEngine::Current().References;
    const auto it = cReferences.find(aObjectFormId);
    if (it == cReferences.end())
        return std::nullopt;
    const auto cItem = it->second.Items.find(aItemFormId);
    ObjectChange change{
        .Type = ObjectChange::Kind::kItems, .Object = SessionIdOf(aObjectFormId), .Item = ItemId(aItemFormId).value_or(GameId{})};
    change.Value = cItem != it->second.Items.end() ? static_cast<uint32_t>(cItem->second) : 0;
    for (const auto& cStack : it->second.Stacks)
        if (cStack.BaseId == change.Item)
            change.Stacks.push_back(cStack);
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
          Wait(aWorld, aWorld.GetDispatcher(), Engine),
          Scene(aWorld, aWorld.GetDispatcher(), Engine)
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
    GuestScene Scene;
};

GuestSeam::GuestSeam(World& aWorld) noexcept : m_world(aWorld)
{
    auto& dispatcher = aWorld.GetDispatcher();
    m_connected = dispatcher.sink<ConnectedEvent>().connect<&GuestSeam::OnConnected>(this);
    m_disconnected = dispatcher.sink<DisconnectedEvent>().connect<&GuestSeam::OnDisconnected>(this);
    m_playerNative = dispatcher.sink<NotifyRoutedCall>().connect<&GuestSeam::OnPlayerNative>(this);
}

void GuestSeam::OnPlayerNative(const NotifyRoutedCall& acMessage) noexcept
{
    if (!m_pGuest || acMessage.To != CallTo::kOwner)
        return;
    // GuestService::OnPlayerNative -> GuestEngine::RunPlayerNative -> GlobalCall::QueueCall.
    if (const auto cCall = Utils::ToLocalCall(acMessage.Call))
        FakeEngine::Current().Queue(*cCall, FakeEngine::kPlayer);
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
