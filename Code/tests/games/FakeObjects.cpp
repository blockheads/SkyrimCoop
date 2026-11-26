#include <ScriptMotion.h>
#include <ContainerCount.h>
#include <world/LidUnposedEvent.h>
#include <core/MenuEvent.h>
#include <CheckedPlacement.h>
#include <FormChangeRows.h>
#include <world/EnabledRow.h>
#include <core/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include "FakeDialogue.h"
#include <core/PlayerTravelQueuedEvent.h>
#include <scripts/StoryEvents.h>

#include <items/ContainerChangeEvent.h>
#include <core/DifficultyChangeEvent.h>
#include <world/ActivateEvent.h>
#include <core/SessionIds.h>
#include <combat/AddTargetEvent.h>
#include <actorstate/AiStateChangeEvent.h>
#include <magic/EffectVisualsChangeEvent.h>
#include <items/EquipmentChangeEvent.h>
#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorValueChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <characters/ReferenceLoadedEvent.h>
#include <actorstate/DeathActions.h>
#include <combat/HitEvent.h>
#include <magic/ProjectileLaunchedEvent.h>
#include <magic/SpellCastEvent.h>
#include <items/InventoryChangeEvent.h>
#include <world/LockChangeEvent.h>
#include <items/LooseItemEvents.h>
#include <scripts/CallEndedEvent.h>
#include <scripts/RoutedCallEvent.h>
#include <core/PlayerProgressEvent.h>
#include <core/EngineNames.h>
#include <travel/RemoveWaypointEvent.h>
#include <travel/SetWaypointEvent.h>
#include <time/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <scripts/PartyNatives.h>
#include <scripts/SystemQuests.h>
#include <scripts/SystemQuestLearnedEvent.h>
#include <core/ScriptValues.h>
#include <array>
#include <ranges>
#include <cstdio>
#include <items/Overrides.h>
#include <join/GameLoadedEvent.h>
#include <core/PlayerFormChangeEvent.h>
#include <join/SaveService.h>
#include <Wire/Buffer.h>
#include <core/World.h>
#include <core/FormIds.h>
#include <CriticalStageRules.h>
#include <animation/DeferredClips.h>
#include <DisplayNames.h>
#include <FreedWornList.h>
#include <Diagnostics/HeadTrackRead.h>
#include <CopyCombat.h>
#include <magic/InterruptCastEvent.h>
#include <characters/ActorHeadUpdateEvent.h>
#include <movement/CopyStep.h>
#include <magic/OwnerEffects.h>
#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>
#include <mounts/MountEvent.h>
#include <join/SaveWrittenEvent.h>
#include <animation/GraphState.h>
#include <animation/GraphStream.h>
#include <bit>
#include <set>

using namespace PartyRules;

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
uint32_t EnabledValue(const FakeEngine::Reference& acReference)
{
    return EnabledRow::Pack({.Enabled = !acReference.Disabled, .Destroyed = acReference.Destroyed});
}

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

// ReferenceStates' motion row: what a script last set.
ObjectChange MotionRow(uint32_t aObject, const FakeEngine::Reference& acReference)
{
    return {.Type = ObjectChange::Kind::kMotion, .Object = SessionIdOf(aObject), .Value = acReference.Motion};
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
    for (auto& [cId, cActor] : Actors)
        if (cActor.Cell == aCell)
            cActor.NotLoadedYet = false;
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

void FakeEngine::ScriptSetsEnabled(uint32_t aReference, bool aEnabled)
{
    auto& reference = References.at(aReference);
    reference.Disabled = !aEnabled;
    reference.Has3D = false; // Disable drops it, Enable queues its load (Finishes3D)
    Changed.insert(aReference);
    Recorded.push_back({.Type = ObjectChange::Kind::kEnabled, .Object = SessionIdOf(aReference), .Value = EnabledValue(reference)});
}

void FakeEngine::ScriptSetsDestroyed(uint32_t aReference, bool aDestroyed)
{
    auto& reference = References.at(aReference);
    reference.Destroyed = aDestroyed;
    Changed.insert(aReference);
    Recorded.push_back({.Type = ObjectChange::Kind::kEnabled, .Object = SessionIdOf(aReference), .Value = EnabledValue(reference)});
}

void FakeEngine::ClosesChestMenu()
{
    const uint32_t cChest = std::exchange(ChestMenuOf, 0);
    auto& chest = References.at(cChest);
    chest.Open = false;
    if (chest.Has3D)
        chest.Lid = 0.f;
    else // our auto-close on a disabled chest (LiveMenuHooks)
    {
        chest.Unposed = true;
        World::Get().GetRunner().Trigger(LidUnposedEvent{cChest});
    }
    if (!IsGuest)
        Recorded.push_back(OpenRow(cChest, chest));
    World::Get().GetRunner().Trigger(MenuEvent{"ContainerMenu", false});
}

void FakeEngine::Finishes3D(uint32_t aReference)
{
    auto& reference = References.at(aReference);
    reference.Has3D = true;
    if (reference.DeferredMotion) // ApplyDeferredMotion, on TESObjectLoadedEvent
        reference.Motion = std::exchange(reference.DeferredMotion, 0u);
    if (reference.Chest)
        reference.Lid = reference.Unposed ? kPartOpen : reference.Open ? 1.f : 0.f;
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

void FakeEngine::ScriptPlaysAnimation(uint32_t aObject, const std::string& acEvent)
{
    References.at(aObject).GraphEvents.push_back(acEvent);
    if (!IsGuest)
        Recorded.push_back({.Type = ObjectChange::Kind::kAnimation, .Object = SessionIdOf(aObject), .Event = acEvent});
}

void FakeEngine::PullsLever(uint32_t aObject)
{
    const auto it = References.find(aObject);
    if (it == References.end() || !it->second.Lever || it->second.ActivationBlocked)
        return;
    it->second.GraphEvents.push_back(kPull);
    if (IsGuest)
        return;
    // HookActivate's pick, recorded by HostService::OnActivate with its puller.
    ObjectChange pull{.Type = ObjectChange::Kind::kAnimation, .Object = SessionIdOf(aObject), .Event = kPull};
    pull.Value = Utils::GetSessionIdByFormId(kPlayer).value_or(0);
    Recorded.push_back(std::move(pull));
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

void FakeEngine::ScriptSetsMotionType(uint32_t aObject, uint32_t aType)
{
    auto& reference = References.at(aObject);
    if (!reference.Has3D) // 56332: "has no 3d", nothing changed
        return;
    reference.Motion = ScriptMotion::Pack(aType, false);
    if (IsGuest)
        return;
    Changed.insert(aObject);
    Recorded.push_back(MotionRow(aObject, reference));
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
    if (cReference.Motion)
        rows.push_back(MotionRow(aReference, cReference));
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
    case ObjectChange::Kind::kOpenState: // ApplyOpen: SetOpenState plays it; with no 3D, LidUnposedEvent
        reference.Open = acChange.Value != 0;
        if (reference.Chest && reference.Has3D)
            reference.Lid = reference.Open ? 1.f : 0.f;
        else if (reference.Chest)
        {
            reference.Unposed = true;
            World::Get().GetRunner().Trigger(LidUnposedEvent{it->first});
        }
        break;
    case ObjectChange::Kind::kHarvested: reference.Harvested = acChange.Value != 0; break;
    case ObjectChange::Kind::kMotion: // ApplyMotion: on its 3D's bodies; with none yet, when it loads
        if (!reference.Has3D)
            reference.DeferredMotion = acChange.Value;
        else
            reference.Motion = acChange.Value;
        break;
    case ObjectChange::Kind::kAnimation: // the graph refuses an event while its own clip runs (measured on the spike wall)
        if (reference.InClip)
            return false;
        reference.GraphEvents.push_back(acChange.Event);
        break;
    case ObjectChange::Kind::kEnabled: // Disable drops the 3D, Enable queues its load (Finishes3D)
        if (reference.Disabled != ((acChange.Value & 1) == 0))
            reference.Has3D = false;
        reference.Disabled = !EnabledRow::Unpack(acChange.Value).Enabled;
        reference.Destroyed = EnabledRow::Unpack(acChange.Value).Destroyed;
        break;
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
            rows.push_back({.Type = ObjectChange::Kind::kEnabled, .Object = CreatedId(cId), .Value = EnabledValue(cReference)});
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

void FakeEngine::ActorUnequipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept
{
    if (auto it = Actors.find(aActorFormId); it != Actors.end() && ::ItemForm(it->second.Items.CurrentMagicEquipment.Shout) == aShoutFormId)
        it->second.Items.CurrentMagicEquipment.Shout = {};
}

void FakeEngine::RevertSelectedSpells(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    actor.Items.CurrentMagicEquipment.LeftHandSpell = actor.Items.CurrentMagicEquipment.RightHandSpell = {};
    actor.HeldSpells[0] = actor.HeldSpells[1] = 0;
}

void FakeEngine::Unload3D(uint32_t aFormId)
{
    Actors.at(aFormId).Loaded = false;
    Actors.at(aFormId).AnimObjects.clear(); // Release3DRelatedData drops its props (43573)
    RevertSelectedSpells(aFormId);
    DropHead(aFormId);
}

void FakeEngine::DropHead(uint32_t aFormId)
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.HeadBuilt = false;
        it->second.HeadTints.reset();
    }
}

void FakeEngine::BuildHeads()
{
    for (auto& [cId, actor] : Actors)
    {
        if (!ActorHas3D(cId))
        {
            actor.HeadBuilt = false;
            actor.HeadTints.reset();
        }
        else if (!actor.HeadBuilt)
        {
            actor.HeadBuilt = true;
            World::Get().GetRunner().Trigger(ActorHeadUpdateEvent{cId});
        }
    }
}

std::string FakeEngine::GetFormName(uint32_t aFormId) const noexcept
{
    if (aFormId == 0x7)
        return PlayerCharacterName();
    const auto it = FormNames.find(aFormId);
    return it != FormNames.end() ? it->second : Name;
}

uint32_t FakeEngine::LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept
{
    for (const auto& [cId, cCell] : Exteriors)
        if (cCell.WorldSpace == aWorldSpaceFormId && cCell.X == aX && cCell.Y == aY)
        {
            if (!AtMainLoop) WorkerCellLoads.push_back(cId);
            return cId;
        }
    return 0;
}

uint32_t FakeEngine::CellAt(uint32_t aWorldSpace, const glm::vec3& acPosition) const noexcept
{
    const auto cAt = GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y);
    // Native movement resolves an already known cell; the bridge lookup can load one from disk.
    for (const auto& [cId, cCell] : Exteriors)
        if (cCell.WorldSpace == aWorldSpace && cCell.X == cAt.X && cCell.Y == cAt.Y)
            return cId;
    return 0;
}

// The loaded grid's centre is the player's cell (TES::centerGridX/Y; the engine's own lag behind it not modelled).
void FakeEngine::LoadAroundPlayer(uint32_t aFrom)
{
    const auto& cPlayer = Actors.at(kPlayer);
    std::set<uint32_t> cells{cPlayer.Cell};
    if (const auto cCentre = Exteriors.find(cPlayer.Cell); cCentre != Exteriors.end())
        for (const auto& [cId, cCell] : Exteriors)
            if (cCell.WorldSpace == cCentre->second.WorldSpace &&
                GridCellCoords::IsCellInGridCell({cCell.X, cCell.Y}, {cCentre->second.X, cCentre->second.Y}, false))
                cells.insert(cId);
    std::set<uint32_t> left = std::exchange(m_playerCells, cells);
    left.insert(aFrom);
    for (const uint32_t cCell : left)
        if (!cells.contains(cCell))
            UnloadCell(cCell);
    for (const uint32_t cCell : cells)
        LoadCell(cCell);
}

void FakeEngine::PlayerStands(uint32_t aCell, const glm::vec3& acPosition)
{
    auto& player = Actors.at(kPlayer);
    const uint32_t cFrom = player.Cell;
    player.Cell = aCell;
    const auto cExterior = Exteriors.find(aCell);
    player.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    std::copy_n(&acPosition.x, 3, player.Position);
    LoadAroundPlayer(cFrom);
}

void FakeEngine::PlayerWalks(const glm::vec3& acPosition)
{
    const auto& cPlayer = Actors.at(kPlayer);
    PlayerStands(CellAt(cPlayer.WorldSpace, acPosition), acPosition);
}
