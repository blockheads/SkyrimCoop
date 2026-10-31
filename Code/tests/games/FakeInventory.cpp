#include <Events/MenuEvent.h>
#include <Games/CheckedPlacement.h>
#include <Events/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include "FakeDialogue.h"
#include <Events/PlayerTravelQueuedEvent.h>
#include <Events/StoryEvents.h>

#include <Events/ContainerChangeEvent.h>
#include <Papyrus/StoryGifts.h>
#include <Games/ItemChanges.h>
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
GameId ServerId(uint32_t aFormId)
{
    return ItemId(aFormId).value_or(GameId{});
}
} // namespace

uint8_t FakeEngine::GetFormType(uint32_t aFormId) const noexcept
{
    const auto it = ItemForms.find(aFormId);
    return it != ItemForms.end() ? it->second.Type : 0;
}

Inventory::Entry& FakeEngine::EntryOf(Actor& aActor, uint32_t aItem)
{
    const GameId cId = ServerId(aItem);
    for (auto& entry : aActor.Items.Entries)
        if (entry.BaseId == cId)
            return entry;
    Inventory::Entry entry;
    entry.BaseId = cId;
    entry.Count = 1;
    return aActor.Items.Entries.emplace_back(entry);
}

void FakeEngine::Equips(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    Equip(aActor, aItem, aSlot);
}

void FakeEngine::Unequips(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    Unequip(aActor, aItem, aSlot);
}

void FakeEngine::RebuildCopyEquipment(uint32_t aActor, uint32_t aRemainingItem)
{
    auto& actor = Actors.at(aActor);
    for (auto& entry : actor.Items.Entries)
        if (::ItemForm(entry.BaseId) != aRemainingItem)
            entry.ExtraWorn = entry.ExtraWornLeft = false;
    actor.EquipmentNeedsReset = true;
}

void FakeEngine::ActorResetEquipment(uint32_t aActorFormId, const MagicEquipment& acMagic) noexcept
{
    if (auto actor = Actors.find(aActorFormId); actor != Actors.end())
    {
        actor->second.EquipmentNeedsReset = false;
        for (auto& item : actor->second.Items.Entries)
            item.ExtraWorn = item.ExtraWornLeft = false;
        actor->second.Items.CurrentMagicEquipment = acMagic;
    }
}

void FakeEngine::ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t) noexcept
{
    ScopedEquipOverride _;
    Equip(aActorFormId, aItemFormId, aEquipSlotFormId);
}

void FakeEngine::ActorUnequipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t) noexcept
{
    ScopedEquipOverride _;
    Unequip(aActorFormId, aItemFormId, aEquipSlotFormId);
}

// EquipHook, then the engine: a hand holds one weapon, an armor takes its slots from what wore them; what it
// replaces comes off through the unequip, under the hook's ScopedUnequipOverride (no event).
void FakeEngine::Equip(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    const auto cActor = Actors.find(aActor);
    const auto cForm = ItemForms.find(aItem);
    if (cActor == Actors.end() || cForm == ItemForms.end())
        return;
    // A torch uses its native left-hand equip type; an explicit right-hand weapon slot cannot equip it.
    if (cForm->second.Type == kLight && aSlot != 0 && aSlot != kLeftHand)
        return;
    if (cActor->second.Copy && !ScopedEquipOverride::IsOverriden())
        return;
    if (cActor->second.Copy && cActor->second.EquipmentNeedsReset)
        return; // A model/outfit rebuild can leave stale equipment state until UnequipAll clears it.
    if (cActor->second.Copy && CountOf(aActor, aItem) <= 0)
        return; // EquipManager::Equip: nothing to put on
    if (!cActor->second.Copy)
        World::Get().GetRunner().Trigger(
            EquipmentChangeEvent{.ActorId = aActor, .ItemId = aItem, .EquipSlotId = aSlot, .Count = 1});

    ScopedUnequipOverride _;
    const bool cWeapon = cForm->second.Type == kWeapon;
    const bool cLeft = cWeapon && aSlot == kLeftHand;
    std::vector<std::pair<uint32_t, uint32_t>> replaced; // item, slot
    for (const auto& cEntry : cActor->second.Items.Entries)
    {
        const uint32_t cWornId = ::ItemForm(cEntry.BaseId);
        const auto cWornForm = ItemForms.find(cWornId);
        if (cWornForm == ItemForms.end() || cWornId == aItem)
            continue;
        if (cWeapon && cWornForm->second.Type == kWeapon && (cLeft ? cEntry.ExtraWornLeft : cEntry.ExtraWorn))
            replaced.emplace_back(cWornId, aSlot);
        else if (!cWeapon && cEntry.ExtraWorn && (cWornForm->second.Slots & cForm->second.Slots))
            replaced.emplace_back(cWornId, 0);
    }
    // Ours (EquipManager::Equip, not applied at once) puts no armor over armor: measured, the copy of a player that
    // swapped iron armor for steel wears the steel after InventoryService's unequip-all, equip, re-equip.
    if (!cWeapon && !replaced.empty() && ScopedEquipOverride::IsOverriden())
        return;
    for (const auto& [cItem, cSlot] : replaced)
        Unequip(aActor, cItem, cSlot);

    auto& entry = EntryOf(cActor->second, aItem);
    (cLeft ? entry.ExtraWornLeft : entry.ExtraWorn) = true;
}

// UnEquipHook, then the engine: off that hand (both for no hand's slot).
void FakeEngine::Unequip(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    const auto cActor = Actors.find(aActor);
    if (cActor == Actors.end())
        return;
    if (cActor->second.Copy && !ScopedEquipOverride::IsOverriden() && !ScopedInventoryOverride::IsOverriden() &&
        !ScopedItemRemovalOverride::IsOverriden())
        return;
    if (!cActor->second.Copy && !ScopedUnequipOverride::IsOverriden())
        World::Get().GetRunner().Trigger(EquipmentChangeEvent{
            .ActorId = aActor, .ItemId = aItem, .EquipSlotId = aSlot, .Count = 1, .Unequip = true});

    const GameId cId = ServerId(aItem);
    for (auto& entry : cActor->second.Items.Entries)
        if (entry.BaseId == cId)
        {
            if (aSlot != kLeftHand)
                entry.ExtraWorn = false;
            if (aSlot != kRightHand)
                entry.ExtraWornLeft = false;
        }
}

std::set<std::tuple<uint32_t, uint32_t, bool>> FakeEngine::Worn(uint32_t aActor) const
{
    std::set<std::tuple<uint32_t, uint32_t, bool>> worn;
    if (const auto it = Actors.find(aActor); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
        {
            if (cEntry.ExtraWorn)
                worn.emplace(cEntry.BaseId.ModId, cEntry.BaseId.BaseId, false);
            if (cEntry.ExtraWornLeft)
                worn.emplace(cEntry.BaseId.ModId, cEntry.BaseId.BaseId, true);
        }
    return worn;
}

// Actor::GetEquipment: the worn entries.
Inventory FakeEngine::GetActorEquipment(uint32_t aActorFormId) const noexcept
{
    Inventory equipment;
    if (const auto it = Actors.find(aActorFormId); it != Actors.end())
    {
        equipment.CurrentMagicEquipment = it->second.Items.CurrentMagicEquipment;
        for (const auto& cEntry : it->second.Items.Entries)
            if (cEntry.IsWorn())
                equipment.Entries.push_back(cEntry);
    }
    return equipment;
}

// TESObjectREFR::GetWornArmor: the worn armors.
Inventory FakeEngine::GetActorWornArmor(uint32_t aActorFormId) const noexcept
{
    Inventory armor = GetActorEquipment(aActorFormId);
    std::erase_if(armor.Entries, [&](const Inventory::Entry& acEntry) {
        return GetFormType(::ItemForm(acEntry.BaseId)) != kArmor;
    });
    return armor;
}

void FakeEngine::AddItems(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    const auto cActor = Actors.find(aActor);
    if (cActor == Actors.end() || !aCount)
        return;
    const GameId cId = ServerId(aItem);
    auto& entries = cActor->second.Items.Entries;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& acEntry) { return acEntry.BaseId == cId; });
    if (it == entries.end())
    {
        if (aCount < 0)
            return;
        Inventory::Entry entry;
        entry.BaseId = cId;
        entries.push_back(entry);
        it = std::prev(entries.end());
    }
    if (it->Count + aCount <= 0)
    {
        if (it->IsWorn())
            Unequip(aActor, aItem, 0);
        std::erase_if(entries, [&](const auto& acEntry) { return acEntry.BaseId == cId; });
        return;
    }
    it->Count += aCount;
}

void FakeEngine::GetsItem(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {
        Inventory::Entry item;
        item.BaseId = ServerId(aItem);
        item.Count = aCount;
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item));
    }
    AddItems(aActor, aItem, aCount);
}

void FakeEngine::QuestGivesItem(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    // As the AddItem native (0x140A2C110): an AddItemFunctor queued on the post-render queue, added in its run.
    const int cFunctor = 0;
    StoryGifts::Queued(StoryGifts::Queue::kPostRender, &cFunctor, true, true);
    const StoryGifts::Run cRun(&cFunctor);
    Inventory::Entry item;
    item.BaseId = ServerId(aItem);
    item.Count = aCount;
    InventoryChangeEvent event(aActor, item);
    event.StoryGiven = StoryGifts::Giving(); // as our add hooks read it
    World::Get().GetRunner().Trigger(std::move(event));
    AddItems(aActor, aItem, aCount);
}

void FakeEngine::OutfitPutsOn(uint32_t aActor, uint32_t aItem)
{
    AddItems(aActor, aItem, 1);
    Equips(aActor, aItem, 0);
}

void FakeEngine::GetsItem(uint32_t aActor, const Inventory::Entry& acItem)
{
    if (!ScopedInventoryOverride::IsOverriden())
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, acItem));
    AddEntry(aActor, acItem);
}

void FakeEngine::LosesItem(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {
        Inventory::Entry item;
        item.BaseId = ServerId(aItem);
        item.Count = -aCount;
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item));
    }
    AddItems(aActor, aItem, -aCount);
}

void FakeEngine::Drops(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    // The inventory's Drop takes the stack the player picked: one with extra data (a name, tempering) as it is.
    if (const auto cStacks = EntriesOf(aActor, aItem); !cStacks.empty())
    {
        Inventory::Entry item = cStacks.front();
        item.Count = -aCount;
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item, true));
        ScopedInventoryOverride _;
        AddEntry(aActor, item);
        return;
    }
    Inventory::Entry item;
    item.BaseId = ServerId(aItem);
    item.Count = -aCount;
    World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item, true));
    ScopedInventoryOverride _;
    AddItems(aActor, aItem, -aCount);
}

uint32_t FakeEngine::DropsInWorld(const Inventory::Entry& acItem)
{
    Inventory::Entry removed = acItem;
    removed.Count = -acItem.Count;
    World::Get().GetRunner().Trigger(InventoryChangeEvent(kPlayer, removed, true));
    {
        ScopedInventoryOverride _;
        AddEntry(kPlayer, removed);
    }
    const uint32_t cId = PlaceLoose(Actors.at(kPlayer).Cell, acItem);
    if (IsGuest)
        World::Get().GetRunner().Trigger(ItemDroppedEvent{cId});
    else
        RecordCreated(cId);
    return cId;
}

void FakeEngine::PicksUp(uint32_t aObject)
{
    const auto it = Loose.find(aObject);
    if (it == Loose.end())
        return;
    if (IsGuest && !PickUpGranted && HostDecides.contains(aObject))
    {
        World::Get().GetRunner().Trigger(ItemPickUpEvent{aObject});
        return;
    }
    Inventory::Entry cItem = it->second.Item;
    cItem.IsQuestItem = it->second.QuestObject;
    Loose.erase(it);
    HostDecides.erase(aObject);
    m_freedTemporary.insert(aObject);
    World::Get().GetRunner().Trigger(InventoryChangeEvent(kPlayer, cItem));
    {
        ScopedInventoryOverride _;
        AddEntry(kPlayer, cItem);
    }
    if (!IsGuest)
        RecordCreated(aObject);
}

std::optional<GameId> FakeEngine::CreateLoose(const Inventory::Entry& acItem, uint32_t aCell)
{
    if (!Attached.contains(aCell))
        return std::nullopt;
    // As MakeLooseItem: no form here for the item (a recipe this game was not told), none made.
    if (const uint32_t cBase = ::ItemForm(acItem.BaseId); !cBase || MissingForms.contains(cBase))
        return GameId{};
    const uint32_t cId = PlaceLoose(aCell, acItem);
    RecordCreated(cId);
    return CreatedId(cId);
}

uint32_t FakeEngine::PlaceLoose(uint32_t aCell, const Inventory::Entry& acItem)
{
    const uint32_t cId = NewTemporary();
    Loose[cId] = {aCell, acItem};
    return cId;
}

std::optional<Inventory::Entry> FakeEngine::TakeLoose(const GameId& acObject)
{
    const uint32_t cId = 0xFF000000 | acObject.BaseId;
    const auto it = acObject.ModId == ObjectChange::kCreatedModId ? Loose.find(cId) : Loose.end();
    if (it == Loose.end())
        return std::nullopt;
    const Inventory::Entry cItem = it->second.Item;
    Loose.erase(it);
    m_freedTemporary.insert(cId);
    RecordCreated(cId);
    return cItem;
}

bool FakeEngine::TakeQuestObject(const GameId& acObject)
{
    const uint32_t cId = 0xFF000000 | acObject.BaseId;
    const auto it = acObject.ModId == ObjectChange::kCreatedModId ? Loose.find(cId) : Loose.end();
    if (it == Loose.end() || !it->second.QuestObject)
        return false;
    PicksUp(cId);
    return true;
}

void FakeEngine::RecordCreated(uint32_t aObject)
{
    const auto it = Loose.find(aObject);
    if (it == Loose.end())
    {
        // ReferenceStates::Deleted
        Recorded.push_back({.Type = ObjectChange::Kind::kEnabled, .Object = CreatedId(aObject), .Value = 2});
        return;
    }
    ObjectChange create{.Type = ObjectChange::Kind::kCreate, .Object = CreatedId(aObject), .Item = it->second.Item.BaseId};
    create.Data = ObjectChange::PackPlacement({.Cell = SessionIdOf(it->second.Cell)});
    create.Stacks = {it->second.Item};
    Recorded.push_back(std::move(create));
}

void FakeEngine::Improves(uint32_t aActor, const Inventory::Entry& acStack, const std::function<void(Inventory::Entry&)>& acChange)
{
    auto& entries = Actors.at(aActor).Items.Entries;
    const auto it = std::ranges::find_if(entries, [&](const Inventory::Entry& acEntry) { return acEntry.CanBeMerged(acStack); });
    if (it == entries.end())
        return;
    const uint32_t cItem = ::ItemForm(acStack.BaseId);
    const auto cBefore = EntriesOf(aActor, cItem);
    Inventory::Entry improved = *it;
    improved.Count = 1;
    acChange(improved);
    if (--it->Count <= 0)
        entries.erase(it);
    {
        ScopedInventoryOverride _;
        AddEntry(aActor, improved);
    }
    // Our hooks on 16145 / 16146: the stacks' difference raised as item changes.
    if (!ScopedInventoryOverride::IsOverriden())
        for (const auto& cChange : ItemChanges::Diff(cBefore, EntriesOf(aActor, cItem)))
            World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, cChange));
}

std::vector<Inventory::Entry> FakeEngine::EntriesOf(uint32_t aActor, uint32_t aItem) const
{
    std::vector<Inventory::Entry> entries;
    for (const auto& cEntry : Actors.at(aActor).Items.Entries)
        if (cEntry.BaseId == ServerId(aItem))
            entries.push_back(cEntry);
    return entries;
}

void FakeEngine::AddEntry(uint32_t aActor, const Inventory::Entry& acItem)
{
    // As AddOrRemoveItem: no form here for the item, nothing added; none for its coating's poison, no coating.
    if (!::ItemForm(acItem.BaseId))
        return;
    Inventory::Entry stack = acItem;
    if (stack.ExtraPoisonId && !::ItemForm(stack.ExtraPoisonId))
        stack.ExtraPoisonId = {}, stack.ExtraPoisonCount = 0;
    stack.ExtraWorn = stack.ExtraWornLeft = false;
    if (!stack.ContainsExtraData())
        return AddItems(aActor, ::ItemForm(acItem.BaseId), acItem.Count);
    auto& entries = Actors.at(aActor).Items.Entries;
    const auto it = std::ranges::find_if(entries, [&](const Inventory::Entry& acEntry) { return acEntry.CanBeMerged(stack); });
    if (it == entries.end())
    {
        if (stack.Count > 0)
            entries.push_back(stack);
        return;
    }
    it->Count += stack.Count;
    if (it->Count <= 0)
        entries.erase(it);
}

int32_t FakeEngine::CountOf(uint32_t aActor, uint32_t aItem) const
{
    int32_t count = 0;
    // A brewed potion by its recipe (ItemIds).
    const auto cBrewed = Brewed.find(aItem);
    if (const auto it = Actors.find(aActor); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
            if (cBrewed != Brewed.end() ? cEntry.BaseId == cBrewed->second.Id() : cEntry.BaseId.BaseId == (aItem & 0x00FFFFFF))
                count += cEntry.Count;
    return count;
}

std::map<std::pair<uint32_t, uint32_t>, int32_t> FakeEngine::Carried(uint32_t aActor) const
{
    std::map<std::pair<uint32_t, uint32_t>, int32_t> carried;
    if (const auto it = Actors.find(aActor); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
            carried[{cEntry.BaseId.ModId, cEntry.BaseId.BaseId}] += cEntry.Count;
    return carried;
}

// Added (a worn one equipped, as SetInventory's adds do) or removed. A removal takes from a stack with extra data only
// when it names that stack's own list (InventoryChanges::RemoveItem 16059; a list rebuilt from the entry matched none
// in Skyrim, 2026-10-30): the bridge's TESObjectREFR::AddOrRemoveItem finds it.
void FakeEngine::ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept
{
    const uint32_t cItem = ::ItemForm(aItem.BaseId);
    if (aItem.Count < 0 && aItem.ContainsExtraData())
    {
        // As TESObjectREFR::AddOrRemoveItem: the carried stack's own list, found by ItemChanges::FindStack.
        auto& entries = Actors.at(aObjectFormId).Items.Entries;
        const auto cStack = ItemChanges::FindStack(entries, aItem);
        if (!cStack)
            return;
        entries[*cStack].Count += aItem.Count;
        if (entries[*cStack].Count <= 0)
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(*cStack));
        return;
    }
    AddEntry(aObjectFormId, aItem);
    if (aItem.Count > 0 && aItem.IsWorn())
    {
        ScopedEquipOverride _;
        Equip(aObjectFormId, cItem, aItem.ExtraWornLeft ? kLeftHand : kRightHand);
    }
}

WeaponState FakeEngine::GetWeaponState(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Weapon : WeaponState::Sheathed;
}

void FakeEngine::SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Weapon = aDraw ? WeaponState::Drawn : WeaponState::Sheathed;
}

