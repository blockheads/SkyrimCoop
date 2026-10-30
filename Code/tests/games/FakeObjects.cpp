#include <Events/MenuEvent.h>
#include <Games/CheckedPlacement.h>
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
    if (aCount > 0)
        aReference.Items[aItem] = aCount;
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

ObjectChange LockRow(uint32_t aReference, const FakeEngine::Reference& acReference)
{
    ObjectChange row{.Type = ObjectChange::Kind::kLock, .Object = SessionIdOf(aReference)};
    row.Value = (acReference.Locked ? kLocked : 0) | acReference.LockLevel;
    return row;
}

// ReferenceStates' open row: 1 open or opening.
ObjectChange OpenRow(uint32_t aDoor, const FakeEngine::Reference& acReference)
{
    ObjectChange row{.Type = ObjectChange::Kind::kOpenState, .Object = SessionIdOf(aDoor)};
    row.Value = acReference.Open ? 1 : 0;
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
    Attached.insert(aCell);
    InMemory.insert(aCell);
}

// An actor Actor::Create made goes with it, unless persistent (copy-contract.md, "Players' copies"); a placeatme
// reference stays.
void FakeEngine::UnloadCell(uint32_t aCell)
{
    Attached.erase(aCell);
    std::vector<uint32_t> left;
    for (const auto& [cId, cActor] : Actors)
        if (cActor.Created && cActor.Cell == aCell && !cActor.Persistent)
            left.push_back(cId);
    for (const uint32_t cId : left)
        DeleteActor(cId);
}

void FakeEngine::PlayerSetsLock(uint32_t aReference, bool aLocked, uint8_t aLevel)
{
    if (IsGuest)
    {
        References.at(aReference).Locked = aLocked;
        References.at(aReference).LockLevel = aLevel;
        Changed.insert(aReference);
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

void FakeEngine::SetLock(uint32_t aReference, bool aLocked, uint8_t aLevel)
{
    auto& reference = References.at(aReference);
    reference.Locked = aLocked;
    reference.LockLevel = aLevel;
    Changed.insert(aReference);
    Recorded.push_back(LockRow(aReference, reference));
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

std::vector<ObjectChange> FakeEngine::RowsOf(uint32_t aReference) const
{
    const auto& cReference = References.at(aReference);
    std::vector<ObjectChange> rows{LockRow(aReference, cReference)};
    if (cReference.Door)
        rows.push_back(OpenRow(aReference, cReference));
    for (const auto& [cItem, cCount] : cReference.Items)
        rows.push_back(ItemsRow(aReference, cReference, cItem));
    return rows;
}

bool FakeEngine::Apply(const ObjectChange& acChange, bool aLoad)
{
    const auto it = References.find(FormIdOf(acChange.Object));
    if (it == References.end() || !InMemory.contains(it->second.Cell))
        return false; // its cell never loaded here: "its object is not in this game"
    auto& reference = it->second;
    switch (acChange.Type)
    {
    case ObjectChange::Kind::kRecord: reference.RecordFlags = acChange.Value; break;
    case ObjectChange::Kind::kLock:
        reference.Locked = (acChange.Value & kLocked) != 0;
        reference.LockLevel = static_cast<uint8_t>(acChange.Value & 0xFF);
        break;
    case ObjectChange::Kind::kItems:
        SetCount(reference, ::ItemForm(acChange.Item), static_cast<int32_t>(acChange.Value), acChange.Stacks);
        break;
    case ObjectChange::Kind::kOpenState: reference.Open = acChange.Value != 0; break;
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
    for (const auto& [cId, cReference] : References)
    {
        const bool cNamed = std::ranges::any_of(acChanged, [cId](const ChangedReference& acRef) { return FormIdOf(acRef.Id) == cId; });
        if (cReference.Cell == cCell && (Changed.contains(cId) || cNamed))
            std::ranges::move(RowsOf(cId), std::back_inserter(rows));
    }
    return rows;
}

std::optional<CellStateRequest> FakeEngine::CellChanges(uint32_t aCell) const
{
    if (!Attached.contains(aCell))
        return std::nullopt;
    CellStateRequest request;
    request.Cell = SessionIdOf(aCell);
    for (const uint32_t cId : Changed)
        if (References.at(cId).Cell == aCell)
            request.Changed.push_back({SessionIdOf(cId), 1});
    return request;
}

