// A guest's side in this process: GuestEngine's calls GuestWorldState makes, on the fake engine's world, and
// GuestService's making of the guest's parts as it joins (its clock, weather, world state and dialogue).
#include <core/CreatedIds.h>
#include <physics/BodyRead.h>
#include "FakeEngine.h"
#include <QuestStarts.h>

#include "FakeGuestEngine.h"
#include <physics/IBodyBridge.h>
#include <core/ConnectedEvent.h>
#include <core/DisconnectedEvent.h>
#include <core/UpdateEvent.h>
#include <scripts/StoryEvents.h>
#include <core/FormIds.h>
#include <items/InventoryChangeEvent.h>
#include <items/Overrides.h>
#include <dialogue/DialogueService.h>
#include <time/GuestClock.h>
#include <dialogue/GuestDialogue.h>
#include <join/GuestRecord.h>
#include <join/GuestSaves.h>
#include <scripts/GuestScene.h>
#include <characters/GuestCopies.h>
#include <physics/GuestBodies.h>
#include <time/GuestWeather.h>
#include <crime/GuestEye.h>
#include <travel/GuestLeash.h>
#include <combat/GuestTraps.h>
#include <ports/IPlayerBridge.h>
#include <world/GuestWorldState.h>
#include <time/GuestWait.h>
#include <game/HostService.h>
#include <Messages/Relayed.h>
#include <core/ScriptValues.h>
#include <core/World.h>
#include <core/ScriptValues.h>
#include <Messages/Relayed.h>
#include <core/SessionIds.h>
#include <scripts/PartyRules.h>

FakeGuestEngine::FakeGuestEngine(IBodyBridge& aBodies) noexcept
    : m_bodyBridge(aBodies)
    , m_moving(m_reads, false)
{
}

FakeGuestEngine::~FakeGuestEngine() noexcept = default;

bool FakeGuestEngine::RunScenePackage(const GameId& acPackage) noexcept
{
    FakeEngine::Current().AiDriven = true;
    FakeEngine::Current().ScenePackage = FormIdOf(acPackage);
    return true;
}

void FakeGuestEngine::EndScenePackage() noexcept
{
    FakeEngine::Current().AiDriven = false;
}
bool FakeGuestEngine::RunPlayerNative(const Payloads::RoutedCall& acCall, std::function<void(LocalValue)> aOnReturn) noexcept
{
    // As GuestEngine: GlobalCall::QueueCall, as this player's own; what it returns goes back (the host's question).
    const auto cCall = Utils::ToLocalCall(acCall.Call);
    if (!cCall)
        return false;
    FakeEngine::Current().Queue(*cCall, PartyRules::kRoutedCallCause, std::move(aOnReturn));
    return true;
}

bool FakeGuestEngine::Adopting(const GameId& acObject) const noexcept
{
    return std::ranges::any_of(m_adopted, [&acObject](const BodyKey& acKey) { return acKey.Object == acObject; });
}

// The engine's reads as the fake engine's (GuestEngine and HostEngine hold one; HostService's TakeBodies reads the fake
// directly).
BodyReads::BodyReads() noexcept = default;
BodyReads::~BodyReads() noexcept = default;
std::vector<uint32_t> BodyReads::TakeMovedReferences() noexcept
{
    return FakeEngine::Current().TakeMovedReferences();
}
uint32_t BodyReads::PlayerInRagdoll() noexcept
{
    return FakeEngine::Current().PlayerInRagdoll();
}
uint32_t BodyReads::PlayerHolds() noexcept
{
    return FakeEngine::Current().PlayerHolds();
}
std::optional<IBodyReads::Reference> BodyReads::ReadReference(uint32_t aFormId, bool aBodies) noexcept
{
    return FakeEngine::Current().ReadReference(aFormId, aBodies);
}
void BodyReads::WriteNode(uint32_t aFormId, uint8_t aIndex) noexcept
{
    FakeEngine::Current().WriteNode(aFormId, aIndex);
}

void FakeGuestEngine::SetDecidesWorld(bool aOn) noexcept
{
    static uint32_t s_sessions = 0;
    FakeEngine::Current().DecidesWorld = aOn;
    FakeEngine& engine = FakeEngine::Current();
    engine.HoldSession = aOn ? 0 : ++s_sessions; // GuestNpcs::SetHolding
    if (!aOn)
        for (const uint32_t cId : engine.GetLoadedActorFormIds())
            engine.ApplyHeld3D(cId); // HideHeldNpcs
}

// As StoryEvents::ToSend: the fake's members name forms by their ids.
std::optional<StoryEvent> FakeGuestEngine::ReadStoryEvent(const OwnStoryEvent& acEvent) const noexcept
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

void FakeGuestEngine::SetCalendar(const TimeModel& acTime) noexcept
{
    FakeEngine::Current().Calendar = acTime;
}

bool FakeGuestEngine::HoldWeather(const GameId& acWeather) noexcept
{
    // Sky::SetWeather(override) (26241): the forced weather, faded into by the next update; held, kept from aging.
    FakeEngine& engine = FakeEngine::Current();
    engine.ForcedWeather = acWeather;
    engine.WeatherHeld = true;
    if (!engine.LastWeather)
        engine.WeatherAge = 0;
    return true;
}

void FakeGuestEngine::ReleaseWeather() noexcept
{
    FakeEngine::Current().ForcedWeather = {};
    FakeEngine::Current().WeatherHeld = false;
}

GameId FakeGuestEngine::ReadWeatherRegion() const noexcept
{
    const uint32_t cRegion = FakeEngine::Current().WeatherRegion;
    return cRegion ? GameId{0, cRegion} : GameId{};
}

void FakeGuestEngine::HoldEye(const DetectionRules::EyeReading& acReading) noexcept
{
    FakeEngine::Current().HeldEye = acReading;
}

void FakeGuestEngine::ReleaseEye() noexcept
{
    FakeEngine::Current().HeldEye.reset();
}

bool FakeGuestEngine::SetGlobal(const GlobalValue& acGlobal) noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    if (engine.SystemQuestList.IsGlobal(FormIdOf(acGlobal.Id)))
        return false; // as GuestEngine: this player's own
    engine.Globals[FormIdOf(acGlobal.Id)] = acGlobal.Value;
    return true;
}

void FakeGuestEngine::ApplyQuest(const QuestState& acQuest, bool aAsLoad) noexcept
{
    auto& engine = FakeEngine::Current();
    const uint32_t cId = FormIdOf(acQuest.Id);
    if (engine.SystemQuestList.Contains(cId))
        return;
    // As GuestEngine: the host's state starting it here reads its dialogue lines.
    if (QuestStarts::LoadsDialogue(engine.Quests.contains(cId) ? engine.Quests[cId].Flags : 0, acQuest.Flags))
        engine.DialogueLoaded.insert(cId);
    if (!engine.AtMainLoop && engine.Quests[cId] != acQuest)
        engine.WorkerQuestWrites.push_back(cId);
    engine.Quests[cId] = acQuest;
    m_pendingAliases[cId] = acQuest;
    RetryQuestAliases();
}

void FakeGuestEngine::RetryQuestAliases() noexcept
{
    auto& engine = FakeEngine::Current();
    for (auto it = m_pendingAliases.begin(); it != m_pendingAliases.end();)
    {
        bool ready = true;
        auto& aliases = engine.QuestAliasReferences[it->first];
        const auto cPrevious = aliases;
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
        // As TESQuest::ForceRefIntoAlias (25052): a stores-text alias's new fill clears its quest's names' caches.
        for (const auto& [cAlias, cRef] : aliases)
            if (cRef && (engine.AliasFlags[it->first][cAlias] & 0x100) && (!cPrevious.contains(cAlias) || cPrevious.at(cAlias) != cRef))
            {
                engine.ClearAliasNameCaches(it->first);
                break;
            }
        if (!engine.AtMainLoop && aliases != cPrevious)
            engine.WorkerQuestWrites.push_back(it->first);
        it = ready ? m_pendingAliases.erase(it) : std::next(it);
    }
}

bool FakeGuestEngine::HasLiveBody(const GameId& acObject) noexcept
{
    const auto& cBodies = FakeEngine::Current().Bodies;
    const auto it = cBodies.find(BodyKey{.Object = acObject});
    return it != cBodies.end() && !it->second.Held;
}

bool FakeGuestEngine::ApplyObjectChange(const ObjectChange& acChange, bool aLoad) noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    if (acChange.Object.ModId != ObjectChange::kCreatedModId)
        return engine.Apply(acChange, aLoad);
    // The host's loose items: a twin made once (Create), deleted with the host's.
    const auto cTwin = m_created.find(acChange.Object.BaseId);
    const bool cHere = cTwin != m_created.end() && (engine.Loose.contains(cTwin->second) || engine.References.contains(cTwin->second));
    if (acChange.Type == ObjectChange::Kind::kCreate)
    {
        if (cHere)
            return true;
        ObjectChange::Placement placement;
        if (!ObjectChange::UnpackPlacement(acChange.Data, placement))
            return false;
        // Loaded with the host's save as this game joined: a save keeps a created reference's id (as GuestEngine).
        if (const auto cSaved = engine.References.find(0xFF000000 | acChange.Object.BaseId);
            cSaved != engine.References.end() && cSaved->second.Base == FormIdOf(acChange.Item) &&
            !World::Get().ctx().at<CreatedIds>().ToSession(cSaved->first)) // not already another's copy
        {
            m_created[acChange.Object.BaseId] = cSaved->first;
            World::Get().ctx().at<CreatedIds>().Link(cSaved->first, acChange.Object);
            return true;
        }
        // A container, or a script's reference (no item stacks: not a drop).
        if (engine.ContainerBases.contains(FormIdOf(acChange.Item)) || acChange.Stacks.empty())
        {
            m_created[acChange.Object.BaseId] = engine.PlaceContainer(FormIdOf(placement.Cell), FormIdOf(acChange.Item));
            World::Get().ctx().at<CreatedIds>().Link(m_created[acChange.Object.BaseId], acChange.Object);
            return true;
        }
        Inventory::Entry item = acChange.Stacks.empty() ? Inventory::Entry{.BaseId = acChange.Item, .Count = 1} : acChange.Stacks[0];
        const uint32_t cId = engine.PlaceLoose(FormIdOf(placement.Cell), item);
        m_created[acChange.Object.BaseId] = cId;
        World::Get().ctx().at<CreatedIds>().Link(cId, acChange.Object);
        engine.HostDecides.insert(cId);
        return true;
    }
    if (!cHere)
        return false;
    if (const auto cContainer = engine.References.find(cTwin->second); cContainer != engine.References.end())
    {
        if (acChange.Type == ObjectChange::Kind::kContents)
            engine.ApplyContents(cContainer->second, acChange);
        else if (acChange.Type == ObjectChange::Kind::kEnabled && acChange.Value & 2)
        {
            engine.References.erase(cContainer);
            m_created.erase(cTwin);
        }
        else if (acChange.Type == ObjectChange::Kind::kEnabled)
            cContainer->second.Disabled = !(acChange.Value & 1);
        return true;
    }
    if (acChange.Type == ObjectChange::Kind::kEnabled && acChange.Value & 2)
    {
        engine.Loose.erase(cTwin->second);
        engine.HostDecides.erase(cTwin->second);
        m_created.erase(cTwin);
    }
    return true;
}

std::optional<DropItemRequest> FakeGuestEngine::ReadDrop(uint32_t aFormId) noexcept
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

bool FakeGuestEngine::AdoptDrop(uint32_t aTag, const GameId& acObject) noexcept
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
    World::Get().ctx().at<CreatedIds>().Link(cOwn, acObject);
    m_adopted.push_back({.Object = acObject});
    return true;
}

std::vector<BodyKey> FakeGuestEngine::TakeAdoptedDrops() noexcept
{
    return std::exchange(m_adopted, {});
}

std::optional<PickUpRequest> FakeGuestEngine::ReadPickUp(uint32_t aFormId) const noexcept
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

void FakeGuestEngine::PickUp(const NotifyPickUp& acGrant) noexcept
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

std::optional<ObjectChange> FakeGuestEngine::ReadObjectState(uint32_t aObjectFormId, ObjectChange::Kind aKind) const noexcept
{
    return aKind == ObjectChange::Kind::kDestruction ? FakeEngine::Current().DestructionRowOf(aObjectFormId) : std::nullopt;
}

std::optional<ObjectChange> FakeGuestEngine::ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) const noexcept
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

std::vector<uint32_t> FakeGuestEngine::AttachedCells() const noexcept
{
    const auto& cAttached = FakeEngine::Current().Attached;
    return {cAttached.begin(), cAttached.end()};
}

std::optional<CellStateRequest> FakeGuestEngine::ReadCellChanges(uint32_t aCellFormId) const noexcept
{
    return FakeEngine::Current().CellChanges(aCellFormId);
}

bool FakeGuestEngine::IsCellAttached(const GameId& acCell) const noexcept
{
    return FakeEngine::Current().Attached.contains(FormIdOf(acCell));
}

void FakeGuestEngine::FreePlaced() noexcept
{
}

bool FakeGuestEngine::IsPlacementPending(const GameId& acObject) const noexcept
{
    return FakeEngine::Current().PendingPlacements.contains(FormIdOf(acObject));
}

namespace
{
std::vector<BodyKey> KeysOf(std::vector<FakeEngine::BodyPush> aPushes)
{
    std::vector<BodyKey> keys;
    for (const auto& cPush : aPushes)
        if (std::ranges::find(keys, cPush.Key) == keys.end())
            keys.push_back(cPush.Key);
    return keys;
}
} // namespace

std::vector<BodyKey> FakeGuestEngine::TakePushes() noexcept
{
    return KeysOf(std::exchange(FakeEngine::Current().PlayerPushes, {}));
}

std::vector<BodyKey> FakeGuestEngine::TakeHits() noexcept
{
    return KeysOf(std::exchange(FakeEngine::Current().PlayerHits, {}));
}

// The player's ragdoll is keyed by its form id here (0x14), by index alone out of GuestEngine.
IGuestEngine::OwnBodies FakeGuestEngine::TakeOwnBodies(uint64_t, const SessionOf_t&) noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    OwnBodies own;
    own.Moved = std::exchange(engine.OwnMoves, {});
    // Its player's ragdoll, and the host's references its own physics simulates (not keyframed on the host's frames).
    auto bodies = engine.TakeBodies(engine.PhysicsTime(World::Get().GetTransport().GetTick()), [&engine](const BodyKey& acKey) {
        return acKey.Actor == 0x14 || engine.WatchedCorpses.contains(acKey.Actor) || (!acKey.Actor && !engine.Bodies.at(acKey).Held);
    });
    for (BodyFrame& frame : bodies.Awake)
    {
        // A copy's corpse its player grabbed: as an object's bodies, by the copy's session id (as GuestEngine).
        if (!frame.Key.Actor || engine.WatchedCorpses.contains(frame.Key.Actor))
        {
            if (!frame.Key.Actor)
                own.Moved.push_back({.Key = frame.Key, .Position = frame.Position, .Rotation = frame.Rotation});
            own.Objects.push_back(frame);
            continue;
        }
        frame.Key = {.Index = frame.Key.Index};
        own.Ragdoll.push_back(frame);
    }
    for (BodyPose& rest : bodies.Rested)
    {
        if (!rest.Key.Actor || engine.WatchedCorpses.contains(rest.Key.Actor))
        {
            own.ObjectsRested.push_back(rest);
            continue;
        }
        rest.Key = {.Index = rest.Key.Index};
        own.RagdollRested.push_back(rest);
    }
    return own;
}

// As PhysicsBodies::Follow: read as moved from now, so its rest goes out when it lies still.
void FakeGuestEngine::FollowOwn(const BodyKey& acKey) noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    if (const auto it = engine.Bodies.find(acKey); it != engine.Bodies.end() && !it->second.Awake)
        engine.Wake(acKey, 32);
}

std::vector<BodyFrame> FakeGuestEngine::ReadHeld(const SessionOf_t&) noexcept
{
    FakeEngine& engine = FakeEngine::Current();
    const auto it = engine.Grabbed ? engine.Bodies.find(*engine.Grabbed) : engine.Bodies.end();
    if (it == engine.Bodies.end())
        return {};
    // A copy's corpse (keyed by its session id here): every body of its ragdoll, watched until they rest.
    if (it->first.Actor)
    {
        engine.WatchedCorpses.insert(it->first.Actor);
        std::vector<BodyFrame> frames;
        for (const auto& [cKey, cBody] : engine.Bodies)
            if (cKey.Actor == it->first.Actor)
            {
                BodyFrame frame = cBody.Flight.Body;
                frame.Key = cKey;
                frames.push_back(frame);
            }
        return frames;
    }
    BodyFrame frame = it->second.Flight.Body;
    frame.Key = it->first;
    return {frame};
}
