#include <animation/ActorStateRules.h>
#include <animation/GraphStream.h>
#include <core/MenuEvent.h>
#include <CheckedPlacement.h>
#include <core/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include "FakeDialogue.h"
#include "FakeCrime.h"
#include <core/FormIds.h>
#include <core/SessionIds.h>
#include <core/PlayerTravelQueuedEvent.h>
#include <scripts/StoryEvents.h>

#include <items/ContainerChangeEvent.h>
#include <scripts/StoryGifts.h>
#include <items/ContainerTransfers.h>
#include <ItemChanges.h>
#include <ScriptEquips.h>
#include <Structs/ItemsPart.h>
#include <core/DifficultyChangeEvent.h>
#include <world/ActivateEvent.h>
#include <combat/AddTargetEvent.h>
#include <actorstate/AiStateChangeEvent.h>
#include <magic/EffectVisualsChangeEvent.h>
#include <items/EquipmentChangeEvent.h>
#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorValueChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <characters/ReferenceLoadedEvent.h>
#include <items/WornResetEvent.h>
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
#include <set>

using namespace PartyRules;

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
    actor.HeldWeapons[0] = actor.HeldWeapons[1] = 0;
    // TESNPC::ResetWornToOutfit (418622) did it: our hook raises it.
    World::Get().GetRunner().Trigger(WornResetEvent{aActor});
}

void FakeEngine::ActorResetEquipment(uint32_t aActorFormId, const MagicEquipment& acMagic) noexcept
{
    if (auto actor = Actors.find(aActorFormId); actor != Actors.end())
    {
        actor->second.EquipmentNeedsReset = false;
        ++actor->second.EquipmentResets;
        actor->second.HeldWeapons[0] = actor->second.HeldWeapons[1] = 0;
        for (auto& item : actor->second.Items.Entries)
            item.ExtraWorn = item.ExtraWornLeft = false;
        EquipSpells(aActorFormId, acMagic);
    }
}

void FakeEngine::ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t) noexcept
{
    ScopedEquipOverride _;
    Equip(aActorFormId, aItemFormId, aEquipSlotFormId);
    // Ours forces the equip (abForceEquip): 16066 marks the worn list cannot wear.
    if (const auto actor = Actors.find(aActorFormId); actor != Actors.end() &&
        std::ranges::any_of(actor->second.Items.Entries, [&](const auto& acEntry) { return ::ItemForm(acEntry.BaseId) == aItemFormId && acEntry.IsWorn(); }))
        actor->second.ForcedWorn.insert(aItemFormId);
}

void FakeEngine::ActorWearStack(uint32_t aActorFormId, const Inventory::Entry& acStack) noexcept
{
    const uint32_t cItem = ::ItemForm(acStack.BaseId);
    const auto cActor = Actors.find(aActorFormId);
    if (cActor == Actors.end() || std::ranges::none_of(cActor->second.Items.Entries, [&](const auto& acEntry) {
            return acEntry.BaseId == acStack.BaseId && acEntry.ExtraName == acStack.ExtraName && acEntry.Count > 0; }))
        return;
    ScopedEquipOverride _;
    Equip(aActorFormId, cItem, 0);
    for (auto& entry : cActor->second.Items.Entries)
        if (entry.BaseId == acStack.BaseId)
            entry.ExtraWorn = entry.ExtraName == acStack.ExtraName;
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
    // A torch's slot is always LeftHand (17619), so 38001 marks it worn left; a two-hander's BothHands, worn right.
    const bool cLeft = (cWeapon && aSlot == kLeftHand && !cForm->second.TwoHanded) || cForm->second.Type == kLight;
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
    if ((cWeapon || cForm->second.Type == kLight) && ActorHas3D(aActor))
    {
        if (cWeapon && cForm->second.TwoHanded)
            cActor->second.HeldWeapons[0] = aItem; // AddEquippedObject (39192): BothHands covers both hands
        cActor->second.HeldWeapons[cLeft ? 0 : 1] = aItem;
    }
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

    for (auto& held : cActor->second.HeldWeapons)
        if (held == aItem)
            held = 0;
    cActor->second.ForcedWorn.erase(aItem);
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
    if (aCount < 0) // removed to nowhere: the alias is unbound and freed with its list
        AliasStacks.erase({aActor, aItem});
    const GameId cId = ServerId(aItem);
    auto& entries = cActor->second.Items.Entries;
    // Plain items: the item's entry with no extra data of its own (a named or tempered stack is its own list).
    const auto cPlain = [&](const Inventory::Entry& acEntry) {
        Inventory::Entry bare = acEntry;
        bare.ExtraWorn = bare.ExtraWornLeft = bare.IsQuestItem = false;
        return acEntry.BaseId == cId && !bare.ContainsExtraData();
    };
    auto it = std::find_if(entries.begin(), entries.end(), cPlain);
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
        std::erase_if(entries, cPlain);
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

void FakeEngine::QuestEquipsItem(uint32_t aActor, uint32_t aItem)
{
    const int32_t cBefore = CountOf(aActor, aItem);
    if (cBefore == 0)
        AddItems(aActor, aItem, 1);
    Equips(aActor, aItem, 0);
    if (auto event = ScriptEquips::Arrival(aActor, ServerId(aItem), cBefore, CountOf(aActor, aItem), true))
        World::Get().GetRunner().Trigger(std::move(*event));
}

void FakeEngine::OutfitPutsOn(uint32_t aActor, uint32_t aItem)
{
    AddItems(aActor, aItem, 1);
    if (const auto actor = Actors.find(aActor); actor != Actors.end())
        actor->second.Outfit.insert(aItem);
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
    AddItems(aActor, aItem, -aCount); // the engine's own (a script's, the console's): a full menu update, measured
}

void FakeEngine::LosesItem(uint32_t aActor, const Inventory::Entry& acItem)
{
    Inventory::Entry item = acItem;
    item.Count = -acItem.Count;
    if (!ScopedInventoryOverride::IsOverriden())
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item));
    AddOrRemoveStack(aActor, item); // the engine's own: a full menu update, measured
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
    const bool cAliasHeld = it->second.Alias.has_value() || it->second.QuestObject;
    if (it->second.Alias)
        AliasStacks[{kPlayer, ::ItemForm(cItem.BaseId)}] = *it->second.Alias;
    // PlayerCharacter::PickUpObject (40533), before the item goes: a created item's StealAlarm names a loaded actor
    // owning it, none no alarm; a guest's CrimeBridge sends it to the host (HookSteal).
    if (IsGuest && it->second.Owner)
        for (const auto& [cId, cActor] : Actors)
            if (cId != kPlayer && cActor.Base == it->second.Owner && Attached.contains(cActor.Cell))
            {
                CrimeRequest theft;
                theft.Action = CrimeAction::Theft;
                theft.TargetActor = Utils::GetSessionIdByFormId(cId).value_or(0);
                theft.Item = cItem.BaseId;
                theft.Owner = ToSession(it->second.Owner).value_or(GameId{});
                theft.Count = static_cast<uint32_t>(cItem.Count);
                theft.AllowWarning = true;
                World::Get().ctx().at<FakeCrime&>().Actions.push_back(std::move(theft));
                break;
            }
    Loose.erase(it);
    HostDecides.erase(aObject);
    m_freedTemporary.insert(aObject);
    InventoryChangeEvent arrival(kPlayer, cItem);
    arrival.AliasHeld = cAliasHeld;
    World::Get().GetRunner().Trigger(std::move(arrival));
    {
        ScopedInventoryOverride _;
        AddEntry(kPlayer, cItem);
    }
    if (!IsGuest)
        RecordCreated(aObject);
}

void FakeEngine::Pickpockets(uint32_t aVictim, uint32_t aItem, bool aCaught)
{
    if (aCaught)
    {
        // A guest's CrimeBridge sends the alarm to the host (HookPickpocket); its own engine raises none.
        if (IsGuest)
        {
            CrimeRequest alarm;
            alarm.Action = CrimeAction::Pickpocket;
            alarm.TargetActor = Utils::GetSessionIdByFormId(aVictim).value_or(0);
            World::Get().ctx().at<FakeCrime&>().Actions.push_back(std::move(alarm));
        }
        return;
    }
    Inventory::Entry item;
    item.BaseId = ToSession(aItem).value_or(GameId{});
    item.Count = 1;
    if (!DecidesWorld && ContainerTransfers::Propose(World::Get(), aVictim, kPlayer, item, true))
        return;
    GetsItem(aVictim, aItem, -1);
    GetsItem(kPlayer, aItem, 1);
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
    if (it == Loose.end() || (!it->second.QuestObject && !it->second.Alias))
        return false;
    PicksUp(cId);
    return true;
}

std::optional<GameId> FakeEngine::DropsPartyItem(const Inventory::Entry& acItem, uint32_t aCopy)
{
    // As HostEngine::DropPartyItem: the host's player's real stack (its name), not a quest object's, in the copy's cell.
    const uint32_t cItem = ::ItemForm(acItem.BaseId);
    const auto cAlias = AliasStacks.find({kPlayer, cItem});
    if (cAlias != AliasStacks.end() && cAlias->second.QuestObject)
        return GameId{};
    if (!Actors.contains(aCopy) || Actors.at(aCopy).Cell != Actors.at(kPlayer).Cell)
        return cAlias != AliasStacks.end() ? std::optional(GameId{}) : std::nullopt;
    auto& entries = Actors.at(kPlayer).Items.Entries;
    const auto it = std::ranges::find_if(entries, [&](const auto& acEntry) {
        return acEntry.BaseId == acItem.BaseId && !acEntry.IsQuestItem && acEntry.ExtraName == acItem.ExtraName && acEntry.Count > 0;
    });
    if (it == entries.end())
        return std::nullopt;
    Inventory::Entry dropped = *it;
    dropped.Count = std::min(std::abs(acItem.Count), it->Count);
    if ((it->Count -= dropped.Count) <= 0)
        entries.erase(it);
    const uint32_t cId = PlaceLoose(Actors.at(aCopy).Cell, dropped);
    if (cAlias != AliasStacks.end())
    {
        Loose.at(cId).Alias = cAlias->second;
        AliasStacks.erase(cAlias);
    }
    RecordCreated(cId);
    return CreatedId(cId);
}

void FakeEngine::AliasCreatesIn(uint32_t aHolder, uint32_t aItem, const AliasStack& acAlias)
{
    Inventory::Entry stack;
    stack.BaseId = ServerId(aItem);
    stack.Count = 1;
    stack.IsQuestItem = acAlias.QuestObject;
    GetsItem(aHolder, stack);
    AliasStacks[{aHolder, aItem}] = acAlias;
}

void FakeEngine::MovesStack(uint32_t aFrom, uint32_t aTo, uint32_t aItem, int32_t aCount, bool aSteal)
{
    // The list moves whole (0x14022C310): its alias is re-keyed to the new holder before the removal below.
    std::optional<AliasStack> alias;
    if (const auto it = AliasStacks.find({aFrom, aItem}); it != AliasStacks.end())
    {
        alias = it->second;
        AliasStacks.erase(it);
        AliasStacks[{aTo, aItem}] = *alias;
    }
    Inventory::Entry stack;
    stack.BaseId = ServerId(aItem);
    stack.IsQuestItem = alias && alias->QuestObject;
    stack.Count = -aCount;
    if (const auto cRef = References.find(aFrom); cRef != References.end() && !Actors.contains(aFrom))
    {
        // A chest's (SetItems keeps its stacks with extra data): the moved one's name with it.
        std::vector<Inventory::Entry> stacks;
        for (const auto& cStack : cRef->second.Stacks)
            if (cStack.BaseId == stack.BaseId)
                stacks.push_back(cStack);
        if (const auto it = std::ranges::find_if(stacks, [](const auto& acStack) { return acStack.Count > 0; }); it != stacks.end())
        {
            stack.ExtraName = it->ExtraName;
            it->Count -= aCount;
            std::erase_if(stacks, [](const auto& acStack) { return acStack.Count <= 0; });
        }
        ScopedInventoryOverride _;
        SetItems(aFrom, aItem, cRef->second.Items[aItem] - aCount, stacks);
    }
    else
        GetsItem(aFrom, stack);
    stack.Count = aCount;
    // 16059's kSteal with a target: the owner (an actor's leveled form, else its base) on the moved list.
    if (const auto cActor = Actors.find(aFrom); aSteal && cActor != Actors.end())
        stack.ExtraOwner = GameId(0, cActor->second.LeveledBase ? cActor->second.LeveledBase : cActor->second.Base);
    if (!ScopedInventoryOverride::IsOverriden())
    {
        InventoryChangeEvent arrival(aTo, stack);
        arrival.AliasHeld = alias.has_value();
        World::Get().GetRunner().Trigger(std::move(arrival));
    }
    {
        ScopedInventoryOverride _;
        AddEntry(aTo, stack);
    }
    // OnContainerChanged reaches the alias's script, which runs where the world is decided.
    if (!alias || !DecidesWorld || (aTo != kPlayer && !IsOtherPlayer(aTo)))
        return;
    auto& quest = Quests[alias->Quest];
    quest.Id = GameId{0, alias->Quest};
    quest.CurrentStage = alias->Stage;
    if (std::ranges::find(quest.DoneStages, alias->Stage) == quest.DoneStages.end())
    {
        quest.DoneStages.push_back(alias->Stage);
        std::ranges::sort(quest.DoneStages);
    }
    QuestChanged(alias->Quest);
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
    if (acItem.Count < 0) // removed to nowhere: the alias is unbound and freed with its list
        AliasStacks.erase({aActor, ::ItemForm(acItem.BaseId)});
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

namespace
{
// The base container's entries of the actor's NPC base (FakeEngine::BaseContainers).
const std::map<uint32_t, int32_t>* BaseOf(const FakeEngine& acEngine, uint32_t aActor)
{
    const auto cActor = acEngine.Actors.find(aActor);
    if (cActor == acEngine.Actors.end())
        return nullptr;
    const auto cBase = acEngine.BaseContainers.find(cActor->second.Base);
    return cBase != acEngine.BaseContainers.end() ? &cBase->second : nullptr;
}
} // namespace

int32_t FakeEngine::CountOf(uint32_t aActor, uint32_t aItem) const
{
    int32_t count = 0;
    if (const auto* cpBase = BaseOf(*this, aActor))
        if (const auto it = cpBase->find(aItem); it != cpBase->end())
            count += it->second;
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
    if (const auto* cpBase = BaseOf(*this, aActor))
        for (const auto& [cItem, cCount] : *cpBase)
            carried[{0u, cItem}] += cCount;
    return carried;
}

// Added (a worn one equipped, as SetInventory's adds do) or removed. A removal takes from a stack with extra data only
// when it names that stack's own list (InventoryChanges::RemoveItem 16059; a list rebuilt from the entry matched none
// in Skyrim, 2026-10-30): the bridge's TESObjectREFR::AddOrRemoveItem finds it.
int32_t FakeEngine::TagStackInPlace(uint32_t aObjectFormId, uint32_t aItemFormId, const std::string& acName, int32_t aCount) noexcept
{
    // As TagStackInPlace: the alias's stack (a quest object's too), else a worn one; never a named one.
    const bool cAliasHeld = AliasStacks.contains({aObjectFormId, aItemFormId});
    for (auto& entry : Actors.at(aObjectFormId).Items.Entries)
        if (entry.BaseId == ServerId(aItemFormId) && entry.ExtraName.empty() && entry.Count > 0 && entry.Count <= aCount &&
            (cAliasHeld || entry.IsQuestItem || entry.IsWorn()))
        {
            entry.ExtraName = acName;
            return entry.Count;
        }
    return 0;
}

void FakeEngine::ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept
{
    if (aItem.Count < 0)
        return ChangesStacks(aObjectFormId, [&] { AddOrRemoveStack(aObjectFormId, aItem); });
    AddOrRemoveStack(aObjectFormId, aItem);
}

void FakeEngine::AddOrRemoveStack(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept
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

ActorStateFields FakeEngine::GetActorStateFields(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return {};
    const Actor& cActor = it->second;
    return {cActor.State1, cActor.State2 | uint32_t(cActor.Weapon) << 5, cActor.InKillMove, cActor.InKillMove ? cActor.Killer : 0,
            cActor.InKillMove && (cActor.InPair || cActor.InPairClip), cActor.Dead, cActor.Noise};
}

::Factions FakeEngine::GetActorFactions(uint32_t aFormId) const noexcept
{
    ::Factions factions;
    if (const auto it = Factions.find(aFormId); it != Factions.end())
        for (const auto& [cFaction, cRank] : it->second)
        {
            Faction faction;
            faction.Id = GameId(0, cFaction);
            faction.Rank = static_cast<int8_t>(cRank);
            factions.ExtraFactions.push_back(faction);
        }
    return factions;
}

void FakeEngine::SetActorFactions(uint32_t aFormId, const ::Factions& acFactions) noexcept
{
    auto& factions = Factions[aFormId];
    factions.clear();
    for (const auto& cFaction : acFactions.ExtraFactions)
        factions[cFaction.Id.BaseId] = cFaction.Rank;
}

void FakeEngine::SetCopyActorStateFlags(uint32_t aFormId, uint32_t aFlags1, uint32_t aFlags2) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !it->second.Copy)
        return;
    it->second.State1 = aFlags1;
    it->second.State2 = aFlags2 & ~ActorStateRules::kWeaponMask;
    it->second.Weapon = WeaponState(aFlags2 >> 5 & 7);
}

void FakeEngine::SetCopyNoise(uint32_t aFormId, int32_t aNoise) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end() && it->second.Copy)
    {
        it->second.Noise = aNoise;
        it->second.NoiseHeld = ActorStateRules::CopyNoiseTimer(aNoise) == ActorStateRules::kHeldNoise;
    }
}

void FakeEngine::SetActionValue(uint32_t aFormId, int32_t aNoise) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end() && ActorStateRules::MakesOwnNoise(it->second.Copy))
        it->second.Noise = aNoise;
}

void FakeEngine::CopyEnterKillMove(uint32_t aFormId, uint32_t aKillerFormId) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !it->second.Copy)
        return;
    it->second.InKillMove = true;
    it->second.Killer = aKillerFormId;
}

void FakeEngine::CopyLeaveKillMove(uint32_t aFormId) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !it->second.Copy)
        return;
    it->second.InKillMove = false; // no killer: 38614 queues no kill
    it->second.Killer = 0;
}

void FakeEngine::SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    // On its owner's stream its graph drops its own events (63591's hook): the draw action is refused (37279).
    if (Graph && it->second.Copy && World::Get().GetGraphStreams().OnStream(aFormId))
    {
        if (aDraw || it->second.Weapon >= WeaponState::Drawn)
            it->second.Weapon = aDraw ? WeaponState::WantToDraw : WeaponState::WantToSheathe;
        return;
    }
    if (it->second.RefusesDraw)
    {
        it->second.Weapon = aDraw ? WeaponState::WantToDraw : WeaponState::WantToSheathe;
        return;
    }
    // Already where it was asked to go: nothing plays.
    if (!aDraw && it->second.Weapon == WeaponState::Sheathed)
        return;
    it->second.Weapon = aDraw ? WeaponState::Drawing : WeaponState::Sheathing;
    it->second.WeaponEventIn = WeaponClipFrames;
}

void FakeEngine::ShowActorWeaponDrawn(uint32_t aFormId, bool aDrawn) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Weapon = aDrawn ? WeaponState::Drawn : WeaponState::Sheathed;
        it->second.WeaponEventIn = 0;
    }
}

// As SetCarriedStacks: the stacks with extra data that differ (worn and charge aside) out and in, then the plain count
// (with the base container's entries counted, as 16108 does).
void FakeEngine::SetCopyItemCount(uint32_t aActorFormId, const GameId& acItem, int32_t aCount, const std::vector<Inventory::Entry>& acStacks) noexcept
{
    const auto cActor = Actors.find(aActorFormId);
    if (cActor == Actors.end())
        return;
    ChangesStacks(aActorFormId, [&] { SetCarriedStacks(aActorFormId, acItem, aCount, acStacks); });
}

void FakeEngine::SetCarriedStacks(uint32_t aActorFormId, const GameId& acItem, int32_t aCount, const std::vector<Inventory::Entry>& acStacks) noexcept
{
    const auto cActor = Actors.find(aActorFormId);
    auto& entries = cActor->second.Items.Entries;
    std::vector<Inventory::Entry> missing;
    for (Inventory::Entry stack : acStacks)
        if (stack.Count > 0)
        {
            stack.ExtraWorn = stack.ExtraWornLeft = stack.IsQuestItem = false;
            missing.push_back(stack);
        }
    for (auto& entry : entries)
    {
        if (entry.BaseId != acItem || !entry.ContainsExtraData())
            continue;
        Inventory::Entry bare = entry;
        bare.ExtraWorn = bare.ExtraWornLeft = bare.IsQuestItem = false;
        if (!bare.ContainsExtraData())
            continue;
        const auto it = std::ranges::find_if(missing, [&](const auto& acWanted) { return acWanted.Count > 0 && ItemsPart::SameStack(acWanted, bare); });
        const int32_t cKept = it != missing.end() ? std::min(it->Count, entry.Count) : 0;
        if (it != missing.end())
            it->Count -= cKept;
        entry.Count = cKept;
    }
    std::erase_if(entries, [](const Inventory::Entry& acEntry) { return acEntry.Count <= 0; });
    for (const auto& cStack : missing)
        if (cStack.Count > 0)
            AddEntry(aActorFormId, cStack); // as AddObjectToContainer with GetExtraDataFromItem: its coating's form made here
    const uint32_t cItem = ::ItemForm(acItem);
    const int32_t cOver = CountOf(aActorFormId, cItem) - aCount;
    if (cOver != 0)
    {
        Inventory::Entry plain;
        plain.BaseId = acItem;
        plain.Count = -cOver;
        AddEntry(aActorFormId, plain);
    }
}

uint32_t FakeEngine::GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept
{
    const auto it = Actors.find(aActorFormId);
    if (it == Actors.end() || aSlot >= 2)
        return 0;
    return it->second.HeldWeapons[aSlot] ? it->second.HeldWeapons[aSlot] : it->second.HeldSpells[aSlot];
}

void FakeEngine::EquipSpells(uint32_t aFormId, const MagicEquipment& acMagic)
{
    auto& actor = Actors.at(aFormId);
    actor.Items.CurrentMagicEquipment = acMagic;
    if (ActorHas3D(aFormId))
    {
        actor.HeldSpells[0] = ::ItemForm(acMagic.LeftHandSpell);
        actor.HeldSpells[1] = ::ItemForm(acMagic.RightHandSpell);
    }
}

void FakeEngine::EquipsSpell(uint32_t aActor, const GameId& acSpell, uint32_t aHand)
{
    auto& actor = Actors.at(aActor);
    if (!actor.Copy)
        World::Get().GetRunner().Trigger(EquipmentChangeEvent{.ActorId = aActor, .ItemId = ::ItemForm(acSpell), .IsSpell = true});
    MagicEquipment magic = actor.Items.CurrentMagicEquipment;
    (aHand == 0 ? magic.LeftHandSpell : magic.RightHandSpell) = acSpell;
    EquipSpells(aActor, magic);
}

void FakeEngine::EquipsShout(uint32_t aActor, const GameId& acShout)
{
    auto& actor = Actors.at(aActor);
    if (!actor.Copy)
        World::Get().GetRunner().Trigger(EquipmentChangeEvent{.ActorId = aActor, .ItemId = ::ItemForm(acShout), .Unequip = !acShout, .IsShout = true});
    actor.Items.CurrentMagicEquipment.Shout = acShout;
}
