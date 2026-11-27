// Actor's item hooks: adding, picking up, dropping, equipping, resets.
#include <CopyCombatGroups.h>
#include <Forms/BGSAction.h>
#include <Interface/ItemMenuWatch.h>
#include <animation/ActorMediator.h>
#include <Animation/TESActionData.h>
#include <animation/ReplayState.h>
#include <animation/ActorStateRules.h>
#include <animation/GraphReference.h>
#include <BSAnimationGraphManager.h>
#include <CrashHandler.h>
#include <HookSwitch.h>
#include <BSCore/LockOrder.h>
#include <core/EngineNames.h>
#include <References.h>
#include <characters/ActorPools.h>
#include <EquipManager.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>
#include <Havok/bhkCharacterController.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <movement/CopyStep.h>
#include <Combat/WorldHits.h>
#include <Misc/GameVM.h>
#include <DefaultObjectManager.h>
#include <Forms/TESNPC.h>
#include <Forms/TESFaction.h>
#include <Components/TESActorBaseData.h>
#include <ExtraData/ExtraFactionChanges.h>
#include <Memory.h>
#include <RefrInteraction.h>
#include <Forms/TESLevItem.h>
#include <Combat/CombatController.h>

#include <actorstate/HealthChangeEvent.h>
#include <characters/ActorDeathChangeEvent.h>
#include <Papyrus/GuestScripts.h>
#include <items/InventoryChangeEvent.h>
#include <items/WornResetEvent.h>
#include <items/LooseItemEvents.h>
#include <mounts/MountEvent.h>
#include <mounts/DismountEvent.h>
#include <mounts/BoardEvent.h>
#include <dialogue/DialogueEvent.h>
#include <combat/HitEvent.h>
#include <Combat/HitOwnership.h>
#include <Combat/PartyAggro.h>
#include <magic/RemoveSpellEvent.h>

#include <TES.h>
#include <GuestNpcs.h>
#include <characters/NpcHeldEvent.h>
#include <core/World.h>
#include <ports/IActorBridge.h>
#include <world/ActivatePrompts.h>
#include <dialogue/SpeakerUpdate.h>
#include <Bridges/PapyrusService.h>
#include <death/DeathDrops.h>

#include <Forms/ActorValueInfo.h>
#include <Forms/TESRace.h>

#include <Effects/ValueModifierEffect.h>

#include <Misc/InventoryEntry.h>
#include <ExtraData/ExtraCount.h>
#include <Misc/ActorKnowledge.h>
#include <Crime.h>
#include <crime/DetectionRules.h>

#include <ExtraData/ExtraDataList.h>
#include <ExtraData/ExtraInteraction.h>
#include <ExtraData/ExtraCharge.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraEnchantment.h>
#include <ExtraData/ExtraHealth.h>
#include <ExtraData/ExtraPoison.h>
#include <ExtraData/ExtraSoul.h>
#include <ExtraData/ExtraTextDisplayData.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/AlchemyItem.h>
#include <core/ItemIds.h>
#include <Forms/TESObjectCELL.h>

#include <Structs/Skyrim/AnimationGraphDescriptor_BHR_Master.h>

#include <items/Overrides.h>
#include <BSAnimationGraphManager.h>
#include <ScriptEquips.h>

#include <Hooks/Initializer.h>

using TAddInventoryItem = void(Actor*, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner);

using TPickUpObject = void*(Actor*, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, float aUnk2);

using TDropObject = void*(Actor*, void* apResult, TESBoundObject* apObject, ExtraDataList* apExtraData, int32_t aCount, NiPoint3* apLocation, NiPoint3* apRotation);

static TAddInventoryItem* RealAddInventoryItem = nullptr;

// TESObjectREFR::sub_19688: an item straight into a reference's inventory changes (the alchemy menu's potion, 51354);
// Actor::AddObjectToContainer (37525) calls it too, having reported its add already.
using TAddToInventory = void(TESObjectREFR*, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apFrom);

static TAddToInventory* RealAddToInventory = nullptr;

static thread_local bool t_addReported = false;

static TPickUpObject* RealPickUpObject = nullptr;

static TDropObject* RealDropObject = nullptr;

// An actor got an item: its inventory change, for its copies.
static void ReportAdd(const Actor& acActor, const TESBoundObject& acItem, ExtraDataList* apExtraData, int32_t aCount) noexcept
{
    if (ScopedInventoryOverride::IsOverriden())
        return;
    Inventory::Entry item{};
    item.BaseId = ItemId(acItem.formID).value_or(GameId{});
    item.Count = aCount;
    if (apExtraData)
        TESObjectREFR::GetItemFromExtraData(item, apExtraData);
    InventoryChangeEvent event(acActor.formID, std::move(item));
    event.StoryGiven = GuestScripts::StoryGiving();
    event.AliasHeld = apExtraData && apExtraData->Contains(ExtraDataType::AliasInstanceArray);
    World::Get().GetRunner().Trigger(std::move(event));
}

// Papyrus Actor.EquipItem (54661): an item the actor lacks is added past AddInventoryItem (16053), reported here.
using TScriptEquipItem = void(void*, uint32_t, Actor*, TESForm*, bool, bool);

static TScriptEquipItem* RealScriptEquipItem = nullptr;

// The count 54661's own test (16133) sees, read without creating the changes (16039): with none it adds nothing.
static std::optional<int64_t> HeldCount(const Actor& acActor, const TESForm& acItem) noexcept
{
    if (!acActor.FindContainerChanges())
        return std::nullopt;
    return acActor.GetItemCountInInventory(const_cast<TESForm*>(&acItem));
}

static void HookScriptEquipItem(void* apVm, uint32_t aStackId, Actor* apThis, TESForm* apItem, bool aPreventRemoval, bool aSilent)
{
    const auto cBefore = apThis && apItem ? HeldCount(*apThis, *apItem) : std::nullopt;
    RealScriptEquipItem(apVm, aStackId, apThis, apItem, aPreventRemoval, aSilent);
    if (!cBefore || *cBefore > 0 || ScopedInventoryOverride::IsOverriden())
        return;
    const int64_t cAfter = HeldCount(*apThis, *apItem).value_or(0);
    if (auto event = ScriptEquips::Arrival(apThis->formID, ItemId(apItem->formID).value_or(GameId{}), static_cast<int32_t>(*cBefore),
                                           static_cast<int32_t>(cAfter), GuestScripts::StoryCalling()))
        World::Get().GetRunner().Trigger(std::move(*event));
}

void HookAddInventoryItem(Actor* apThis, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner)
{
    ReportAdd(*apThis, *apItem, apExtraData, aCount);
    t_addReported = true;
    RealAddInventoryItem(apThis, apItem, apExtraData, aCount, apOldOwner);
    t_addReported = false;
}

void HookAddToInventory(TESObjectREFR* apThis, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apFrom)
{
    if (const auto* cpActor = Cast<Actor>(apThis); cpActor && apItem && !t_addReported)
        ReportAdd(*cpActor, *apItem, apExtraData, aCount);
    RealAddToInventory(apThis, apItem, apExtraData, aCount, apFrom);
}

void* HookPickUpObject(Actor* apThis, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, float aUnk2)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {

        Inventory::Entry item{};
        item.BaseId = ItemId(apObject->baseForm->formID).value_or(GameId{});
        item.Count = aCount;

        if (apObject->GetExtraDataList())
            apThis->GetItemFromExtraData(item, apObject->GetExtraDataList());

        // This is here so that objects that are picked up on both clients, aka non temps, are synced through activation sync.
        // The inventory change event should always be sent to the server, otherwise the server inventory won't be updated.
        bool shouldUpdateClients = apObject->IsTemporary() && !ScopedActivateOverride::IsOverriden();

        World::Get().GetRunner().Trigger(InventoryChangeEvent(apThis->formID, std::move(item), false, shouldUpdateClients));
    }

    return RealPickUpObject(apThis, apObject, aCount, aUnk1, aUnk2);
}

void* HookDropObject(Actor* apThis, void* apResult, TESBoundObject* apObject, ExtraDataList* apExtraData, int32_t aCount, NiPoint3* apLocation, NiPoint3* apRotation)
{

    Inventory::Entry item{};
    item.BaseId = ItemId(apObject->formID).value_or(GameId{});
    item.Count = -aCount;

    if (apExtraData)
        apThis->GetItemFromExtraData(item, apExtraData);

    World::Get().GetRunner().Trigger(InventoryChangeEvent(apThis->formID, std::move(item), true));

    ScopedInventoryOverride _;

    void* pResult = RealDropObject(apThis, apResult, apObject, apExtraData, aCount, apLocation, apRotation);
    // The loose item it made (none when it went into a container instead), by its handle.
    if (const TESObjectREFR* pDropped = apResult ? TESObjectREFR::GetByHandle(*static_cast<uint32_t*>(apResult)) : nullptr)
        World::Get().GetRunner().Trigger(ItemDroppedEvent{pDropped->formID});
    return pResult;
}

void Actor::DropObject(TESBoundObject* apObject, ExtraDataList* apExtraData, int32_t aCount, NiPoint3* apLocation, NiPoint3* apRotation) noexcept
{
    spdlog::debug("Dropping object, form id: {:X}, count: {}, actor: {:X}", apObject->formID, aCount, formID);
    BSPointerHandle<TESObjectREFR> result{};
    RealDropObject(this, &result, apObject, apExtraData, aCount, apLocation, apRotation);
}

using TUnequipObject = void(Actor*, void* apUnk1, TESBoundObject* apObject, int32_t aUnk2, void* apUnk3);

static TUnequipObject* RealUnequipObject = nullptr;

void HookUnequipObject(Actor* apThis, void* apUnk1, TESBoundObject* apObject, int32_t aUnk2, void* apUnk3)
{
    RealUnequipObject(apThis, apUnk1, apObject, aUnk2, apUnk3);
}

// Actor::ResetInventory (37322): back to its base's template, outfit taken off and put on again, as enabling
// it does (TESObjectREFR::Enable 19800). A copy's inventory is its owner's: never reset (copy-contract.md).
using TResetInventory = void(Actor*, bool aLeveledOnly);

static TResetInventory* RealResetInventory = nullptr;

void HookResetInventory(Actor* apThis, bool aLeveledOnly)
{
    if (apThis->GetExtension()->IsCopy())
        return;
    const ItemMenuWatch::Change cChange(apThis); // frees the lists an open item menu lists
    RealResetInventory(apThis, aLeveledOnly);
}

// TESNPC::ResetWornToOutfit (418622): worn items off (16100), the hands emptied, the base's outfit on; past our equip hooks.
// Callers: ResetInventory, InitLoadGame, SetLevel, TESNPC::InitWornOn3D 24725 (inventory-changes.md).
// Four byte arguments (24725's call: r8b, r9b, two stack bytes); the fourth picks the best weapons carried.
using TResetWornToOutfit = void(void* apNpc, Actor* apActor, bool aUnused3, bool aEquipBestWeapons, bool aUnused5, bool aKeepTlsFlag);

static TResetWornToOutfit* RealResetWornToOutfit = nullptr;

void HookResetWornToOutfit(void* apNpc, Actor* apActor, bool aUnused3, bool aEquipBestWeapons, bool aUnused5, bool aKeepTlsFlag)
{
    {
        const ItemMenuWatch::Change cChange(apActor); // its unequips merge worn lists (16067)
        RealResetWornToOutfit(apNpc, apActor, aUnused3, aEquipBestWeapons, aUnused5, aKeepTlsFlag);
    }
    if (apActor)
        World::Get().GetRunner().Trigger(WornResetEvent{apActor->formID});
}

using TAddDeathItems = void(Actor*);

static TAddDeathItems* RealAddDeathItems = nullptr;

void HookAddDeathItems(Actor* apThis)
{
    if (apThis->GetExtension()->IsCopy())
        return;

    RealAddDeathItems(apThis);
}

// KillImpl's (37896) only call: the shield, torch and drawn weapons fall. A player's stay in its hands.
using TDropHeldItemsOnDeath = void(Actor*);

static TDropHeldItemsOnDeath* RealDropHeldItemsOnDeath = nullptr;

void HookDropHeldItemsOnDeath(Actor* apThis)
{
    if (DeathDrops::Kept(apThis->GetExtension()->IsPlayer(), World::Get().GetTransport().IsOnline()))
        return;

    RealDropHeldItemsOnDeath(apThis);
}

static Hooks::Initializer s_actorItemHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(37322, &RealResetInventory, HookResetInventory);
        HOOK_ID(54661, &RealScriptEquipItem, HookScriptEquipItem);
        HOOK_ID(37525, &RealAddInventoryItem, HookAddInventoryItem);
        HOOK_ID(19688, &RealAddToInventory, HookAddToInventory);
        HOOK_ID(37521, &RealPickUpObject, HookPickUpObject);
        HOOK_ID(40454, &RealDropObject, HookDropObject);
        HOOK_ID(37975, &RealUnequipObject, HookUnequipObject);
        HOOK_ID(37198, &RealAddDeathItems, HookAddDeathItems);
        HOOK_ID(418622, &RealResetWornToOutfit, HookResetWornToOutfit);
        HOOK_ID(37320, &RealDropHeldItemsOnDeath, HookDropHeldItemsOnDeath);
    });
