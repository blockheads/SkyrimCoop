#include <Games/ContainerCount.h>
#include <Events/MenuEvent.h>
#include <Games/CheckedPlacement.h>
#include <Games/FormChangeRows.h>
#include <Events/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include "FakeDialogue.h"
#include <Events/PlayerTravelQueuedEvent.h>
#include <Events/StoryEvents.h>

#include <Events/ContainerChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/AddTargetEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Games/DeathActions.h>
#include <Events/HitEvent.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Events/SpellCastEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/LooseItemEvents.h>
#include <Events/CallEndedEvent.h>
#include <Events/RoutedCallEvent.h>
#include <Events/PlayerProgressEvent.h>
#include <Games/EngineNames.h>
#include <Events/RemoveWaypointEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <Papyrus/PartyNatives.h>
#include <Papyrus/SystemQuests.h>
#include <Events/SystemQuestLearnedEvent.h>
#include <ScriptValues.h>
#include <array>
#include <ranges>
#include <cstdio>
#include <Games/Overrides.h>
#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Services/SaveService.h>
#include <Wire/Buffer.h>
#include <World.h>
#include <FormIds.h>

using namespace PartyRules;

namespace
{
// A container's count of an item; none held is no entry, as the engine's lists keep it.
void SetCount(FakeEngine::Reference& aReference, uint32_t aItem, int32_t aCount)
{
    if (aCount > 0 || aReference.Rolled.contains(aItem))
        aReference.Items[aItem] = std::max(aCount, 0); // a roll's entry stays, at 0 (16059 on a non-actor)
    else
        aReference.Items.erase(aItem);
}

// And its stacks with extra data, as given (ContainerItems' SetContainerStacks).
void SetCount(FakeEngine::Reference& aReference, uint32_t aItem, int32_t aCount, std::vector<Inventory::Entry> aStacks)
{
    SetCount(aReference, aItem, aCount);
    std::erase_if(aReference.Stacks, [&](const Inventory::Entry& acStack) { return acStack.BaseId == ItemId(aItem); });
    for (auto& stack : aStacks)
        aReference.Stacks.push_back(std::move(stack));
}

std::vector<Inventory::Entry> StacksOf(const FakeEngine::Reference& acReference, uint32_t aItem)
{
    std::vector<Inventory::Entry> stacks;
    for (const auto& cStack : acReference.Stacks)
        if (cStack.BaseId == ItemId(aItem))
            stacks.push_back(cStack);
    return stacks;
}

// ReferenceStates' lock row: the level in the low byte, kLocked above it.
constexpr uint32_t kLocked = 0x100;

// acHolder: the reference holding its lock (FakeEngine::LockHolderOf).
ObjectChange LockRow(uint32_t aReference, const FakeEngine::Reference& acHolder)
{
    ObjectChange row{.Type = ObjectChange::Kind::kLock, .Object = SessionIdOf(aReference)};
    row.Value = (acHolder.Locked ? kLocked : 0) | acHolder.LockLevel;
    return row;
}

// ReferenceStates' open row: 1 open or opening.
ObjectChange OpenRow(uint32_t aDoor, const FakeEngine::Reference& acReference)
{
    ObjectChange row{.Type = ObjectChange::Kind::kOpenState, .Object = SessionIdOf(aDoor)};
    row.Value = acReference.Open ? 1 : 0;
    return row;
}

// ReferenceStates' harvested row: 1 picked.
ObjectChange HarvestedRow(uint32_t aPlant, const FakeEngine::Reference& acReference)
{
    ObjectChange row{.Type = ObjectChange::Kind::kHarvested, .Object = SessionIdOf(aPlant)};
    row.Value = acReference.Harvested ? 1 : 0;
    return row;
}

ObjectChange ItemsRow(uint32_t aContainer, const FakeEngine::Reference& acReference, uint32_t aItem)
{
    ObjectChange row{.Type = ObjectChange::Kind::kItems, .Object = SessionIdOf(aContainer), .Item = ItemId(aItem).value_or(GameId{})};
    const auto it = acReference.Items.find(aItem);
    row.Value = it != acReference.Items.end() ? static_cast<uint32_t>(it->second) : 0;
    row.Stacks = StacksOf(acReference, aItem);
    return row;
}
} // namespace

void FakeEngine::LoadCell(uint32_t aCell)
{
    const bool cAttaching = Attached.insert(aCell).second;
    InMemory.insert(aCell);
    // Cell attach queues each reference's 3D (QueueReference 13057).
    if (cAttaching)
        for (auto& [cId, cActor] : Actors)
            if (cId != kPlayer && cActor.Cell == aCell && !cActor.Initializing)
                Load3D(cId);
}

// An actor Actor::Create made goes with it, unless persistent (copy-contract.md, "Players' copies"); a placeatme
// reference stays.
void FakeEngine::UnloadCell(uint32_t aCell)
{
    Attached.erase(aCell);
    std::vector<uint32_t> left;
    for (auto& [cId, actor] : Actors)
    {
        if (actor.Cell == aCell)
            actor.HeadTints.reset(); // its 3D goes with the cell
        if (actor.Created && actor.Cell == aCell && !actor.Persistent)
            left.push_back(cId);
    }
    for (const uint32_t cId : left)
        DeleteActor(cId);
}

int32_t FakeEngine::ContainerCountOf(uint32_t aContainer, uint32_t aItem) const
{
    const auto& cReference = References.at(aContainer);
    const auto cEntry = cReference.Items.find(aItem);
    return ContainerCount(0, cEntry != cReference.Items.end() ? std::optional(cEntry->second) : std::nullopt, cReference.Rolled.contains(aItem));
}

uint32_t FakeEngine::LockHolderOf(uint32_t aReference) const
{
    const auto it = References.find(aReference);
    return it != References.end() && it->second.LinkedDoor ? it->second.LinkedDoor : aReference;
}

void FakeEngine::PlayerSetsLock(uint32_t aReference, bool aLocked, uint8_t aLevel)
{
    if (IsGuest)
    {
        References.at(LockHolderOf(aReference)).Locked = aLocked;
        References.at(LockHolderOf(aReference)).LockLevel = aLevel;
        Changed.insert(LockHolderOf(aReference));
        World::Get().GetDispatcher().trigger(LockChangeEvent(aReference, aLocked, aLevel, true));
        return;
    }
    SetLock(aReference, aLocked, aLevel);
}

void FakeEngine::PlayerSetsItems(uint32_t aContainer, uint32_t aItem, int32_t aCount, std::vector<Inventory::Entry> aStacks)
{
    if (IsGuest)
    {
        SetCount(References.at(aContainer), aItem, aCount, std::move(aStacks));
        Changed.insert(aContainer);
        World::Get().GetDispatcher().trigger(ContainerChangeEvent{aContainer, aItem});
        return;
    }
    SetItems(aContainer, aItem, aCount, std::move(aStacks));
}

// Recorded on the reference holding the lock (AddLockChange 19512).
void FakeEngine::SetLock(uint32_t aReference, bool aLocked, uint8_t aLevel)
{
    const uint32_t cHolder = LockHolderOf(aReference);
    auto& holder = References.at(cHolder);
    holder.Locked = aLocked;
    holder.LockLevel = aLevel;
    Changed.insert(cHolder);
    Recorded.push_back(LockRow(cHolder, holder));
}

void FakeEngine::ScriptSetsCellOwner(uint32_t aCell, uint32_t aOwner)
{
    CellOwners[aCell] = aOwner;
    if (!IsGuest && FormChangeRows::Sent(FormChangeRows::Form::kCell, FormChangeRows::cCellOwnership, ~0u))
        Recorded.push_back({.Type = ObjectChange::Kind::kOwner, .Object = SessionIdOf(aCell), .Item = SessionIdOf(aOwner)});
}

void FakeEngine::ScriptSetsActorOwner(uint32_t aActor, uint32_t aOwner)
{
    auto& actor = Actors.at(aActor);
    actor.Owner = aOwner;
    actor.OwnerRecorded = true;
    if (!IsGuest && FormChangeRows::Sent(FormChangeRows::Form::kActor, FormChangeRows::cReferenceOwnership, ~0u))
        Recorded.push_back({.Type = ObjectChange::Kind::kOwner, .Object = SessionIdOf(aActor), .Item = SessionIdOf(aOwner)});
}

void FakeEngine::SetItems(uint32_t aContainer, uint32_t aItem, int32_t aCount, std::vector<Inventory::Entry> aStacks)
{
    auto& reference = References.at(aContainer);
    SetCount(reference, aItem, aCount, std::move(aStacks));
    Changed.insert(aContainer);
    Recorded.push_back(ItemsRow(aContainer, reference, aItem));
}

void FakeEngine::SetOpen(uint32_t aDoor, bool aOpen)
{
    auto& reference = References.at(aDoor);
    reference.Open = aOpen;
    Changed.insert(aDoor);
    Recorded.push_back(OpenRow(aDoor, reference));
}

void FakeEngine::ToggleDoor(uint32_t aObject)
{
    const auto it = References.find(aObject);
    if (it == References.end() || !it->second.Door)
        return;
    if (IsGuest)
        it->second.Open = !it->second.Open;
    else
        SetOpen(aObject, !it->second.Open);
}

void FakeEngine::Harvest(uint32_t aObject)
{
    const auto it = References.find(aObject);
    if (it == References.end() || !it->second.Plant || it->second.Harvested)
        return;
    it->second.Harvested = true;
    if (IsGuest)
        return;
    Changed.insert(aObject);
    Recorded.push_back(HarvestedRow(aObject, it->second));
}

uint32_t FakeEngine::PlaceContainer(uint32_t aCell, uint32_t aBase)
{
    const uint32_t cId = NewTemporary();
    References[cId] = {.Cell = aCell, .Base = aBase};
    return cId;
}

uint32_t FakeEngine::ScriptPlacesContainer(uint32_t aCell, uint32_t aBase, const std::map<uint32_t, int32_t>& acRolled)
{
    const uint32_t cId = PlaceContainer(aCell, aBase);
    References.at(cId).Items = acRolled;
    // ReadCreate: its base and where, no contents.
    ObjectChange create{.Type = ObjectChange::Kind::kCreate, .Object = CreatedId(cId), .Item = SessionIdOf(aBase)};
    create.Data = ObjectChange::PackPlacement({.Cell = SessionIdOf(aCell)});
    Recorded.push_back(std::move(create));
    return cId;
}

uint32_t FakeEngine::ScriptPlaces(uint32_t aAt, uint32_t aBase)
{
    const auto cActor = Actors.find(aAt);
    const auto cReference = References.find(aAt);
    const uint32_t cCell = cActor != Actors.end() ? cActor->second.Cell : cReference != References.end() ? cReference->second.Cell : 0;
    const uint32_t cId = PlaceContainer(cCell, aBase);
    if (cActor != Actors.end())
        References.at(cId).Position = {cActor->second.Position[0], cActor->second.Position[1], cActor->second.Position[2]};
    ObjectChange create{.Type = ObjectChange::Kind::kCreate, .Object = CreatedId(cId), .Item = SessionIdOf(aBase)};
    create.Data = ObjectChange::PackPlacement({.Cell = SessionIdOf(cCell)});
    Recorded.push_back(std::move(create));
    return cId;
}

std::optional<ObjectChange> FakeEngine::ContentsOf(uint32_t aContainer) const
{
    const auto it = References.find(aContainer);
    if (it == References.end() || !ContainerBases.contains(it->second.Base))
        return std::nullopt;
    ObjectChange contents{.Type = ObjectChange::Kind::kContents, .Object = ToSessionOrCreated(aContainer).value_or(GameId{})};
    std::vector<ObjectChange::ItemCount> items;
    for (const auto& [cItem, cCount] : it->second.Items)
        items.push_back({ItemId(cItem).value_or(GameId{}), cCount});
    contents.Data = ObjectChange::PackContents(items);
    contents.Stacks = it->second.Stacks;
    return contents;
}

void FakeEngine::ApplyContents(Reference& aContainer, const ObjectChange& acContents)
{
    aContainer.Items.clear();
    for (const auto& cItem : ObjectChange::UnpackContents(acContents.Data))
        SetCount(aContainer, ::ItemForm(cItem.Item), cItem.Count);
    aContainer.Stacks = acContents.Stacks;
}

std::vector<ObjectChange> FakeEngine::RowsOf(uint32_t aReference) const
{
    const auto& cReference = References.at(aReference);
    std::vector<ObjectChange> rows{LockRow(aReference, References.at(LockHolderOf(aReference)))};
    if (cReference.Door)
        rows.push_back(OpenRow(aReference, cReference));
    if (cReference.Plant)
        rows.push_back(HarvestedRow(aReference, cReference));
    for (const auto& [cItem, cCount] : cReference.Items)
        rows.push_back(ItemsRow(aReference, cReference, cItem));
    return rows;
}

bool FakeEngine::Apply(const ObjectChange& acChange, bool aLoad)
{
    if (const auto cCell = CellOwners.find(FormIdOf(acChange.Object)); cCell != CellOwners.end())
    {
        if (acChange.Type != ObjectChange::Kind::kOwner)
            return false;
        cCell->second = FormIdOf(acChange.Item);
        return true;
    }
    // An actor's owner (the party's horse): an actor its cell never loaded is not in this game.
    if (const auto cActor = Actors.find(FormIdOf(acChange.Object)); cActor != Actors.end())
    {
        if (acChange.Type != ObjectChange::Kind::kOwner || !InMemory.contains(cActor->second.Cell))
            return false;
        cActor->second.Owner = FormIdOf(acChange.Item);
        return true;
    }
    if (acChange.Type == ObjectChange::Kind::kMove)
    {
        const auto cBody = Bodies.find(BodyKey{.Object = acChange.Object});
        if (cBody != Bodies.end())
        {
            if (acChange.Data.size() != 6 * sizeof(float))
                return false;
            float pose[6];
            std::memcpy(pose, acChange.Data.data(), sizeof(pose));
            auto& body = cBody->second;
            body.SceneWritesDuringJobs += !AtMainLoop;
            ++body.MoveRows;
            body.NodePosition = body.Flight.Body.Position = {pose[0], pose[1], pose[2]};
            body.NodeRotation = body.Flight.Body.Rotation = glm::quat(glm::vec3(pose[3], pose[4], pose[5]));
            return true;
        }
    }
    const auto it = References.find(FormIdOf(acChange.Object));
    if (it == References.end() || (!InMemory.contains(it->second.Cell) && !it->second.Persistent))
        return false; // its cell never loaded here: "its object is not in this game"
    auto& reference = it->second;
    switch (acChange.Type)
    {
    case ObjectChange::Kind::kRecord: reference.RecordFlags = acChange.Value; break;
    case ObjectChange::Kind::kLock:
    {
        auto& holder = References.at(LockHolderOf(it->first)); // SetLockState through GetLock (20223)
        holder.Locked = (acChange.Value & kLocked) != 0;
        holder.LockLevel = static_cast<uint8_t>(acChange.Value & 0xFF);
        break;
    }
    case ObjectChange::Kind::kItems:
        SetCount(reference, ::ItemForm(acChange.Item), static_cast<int32_t>(acChange.Value), acChange.Stacks);
        break;
    case ObjectChange::Kind::kOpenState: reference.Open = acChange.Value != 0; break;
    case ObjectChange::Kind::kHarvested: reference.Harvested = acChange.Value != 0; break;
    case ObjectChange::Kind::kContents: ApplyContents(reference, acChange); break;
    default: break;
    }
    (void)aLoad; // a state already as the host's is left as it is: the same either way here
    return true;
}

std::optional<std::vector<ObjectChange>> FakeEngine::CellState(const GameId& acCell, const std::vector<ChangedReference>& acChanged) const
{
    const uint32_t cCell = FormIdOf(acCell);
    if (!Attached.contains(cCell))
        return std::nullopt;
    std::vector<ObjectChange> rows;
    // The host's created ones there first, so the guest makes its own (HostEngine::ReadCellState): base, place, state.
    for (const auto& [cId, cReference] : References)
        if (cReference.Cell == cCell && IsCreatedForm(cId) && cReference.Base)
        {
            ObjectChange create{.Type = ObjectChange::Kind::kCreate, .Object = CreatedId(cId), .Item = SessionIdOf(cReference.Base)};
            create.Data = ObjectChange::PackPlacement({.Cell = SessionIdOf(cCell)});
            rows.push_back(std::move(create));
            rows.push_back({.Type = ObjectChange::Kind::kEnabled, .Object = CreatedId(cId), .Value = cReference.Disabled ? 0u : 1u});
        }
    for (const auto& [cId, cReference] : References)
    {
        const bool cNamed = std::ranges::any_of(acChanged, [cId](const ChangedReference& acRef) { return FormIdOf(acRef.Id) == cId; });
        if (cReference.Cell == cCell && (Changed.contains(cId) || Changed.contains(LockHolderOf(cId)) || cNamed))
            std::ranges::move(RowsOf(cId), std::back_inserter(rows));
    }
    for (const auto& [cId, cActor] : Actors)
        if (cActor.Cell == cCell && cActor.OwnerRecorded &&
            FormChangeRows::Sent(FormChangeRows::Form::kActor, FormChangeRows::cReferenceOwnership, ~0u))
            rows.push_back({.Type = ObjectChange::Kind::kOwner, .Object = SessionIdOf(cId), .Item = SessionIdOf(cActor.Owner)});
    return rows;
}

std::optional<CellStateRequest> FakeEngine::CellChanges(uint32_t aCell) const
{
    if (!Attached.contains(aCell))
        return std::nullopt;
    CellStateRequest request;
    request.Cell = SessionIdOf(aCell);
    // Its changed references there, and a load door whose lock (its linked door's, elsewhere) changed.
    for (const auto& [cId, cReference] : References)
        if (cReference.Cell == aCell && (Changed.contains(cId) || Changed.contains(LockHolderOf(cId))))
            request.Changed.push_back({SessionIdOf(cId), 1});
    return request;
}

