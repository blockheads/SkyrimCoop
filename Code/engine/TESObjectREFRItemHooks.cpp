// TESObjectREFR's item hooks: a container's items added, removed, improved.
#include <BSCore/LockOrder.h>
#include <FreedWornList.h>
#include <Memory.h>
#include <animation/GraphReference.h>
#include <Animation/VariableSync.h>
#include <animation/ActorStateRules.h>
#include <animation/ReplayState.h>
#include <Havok/AnimationGraphState.h>
#include <atomic>
#include <cstring>
#include <SaveLoad.h>
#include <HookSwitch.h>

#include <References.h>
#include <items/Overrides.h>
#include <ItemChanges.h>
#include <HiddenCopies.h>
#include <GuestNpcs.h>
#include <ExtraData/ExtraOwnership.h>

#include <core/World.h>
#include <physics/BodyService.h>
#include <Bridges/PapyrusService.h>
#include <world/ActivateEvent.h>
#include <Papyrus/GuestScripts.h>
#include <items/InventoryChangeEvent.h>
#include <animation/ScriptAnimationEvent.h>
#include <world/LockChangeEvent.h>
#include <ContainerCount.h>
#include <characters/ReferenceJumpEvent.h>

#include <ExtraData/ExtraContainerChanges.h>
#include <items/ContainerTransfers.h>
#include <Interface/ItemMenuWatch.h>
#include <Interface/UI.h>
#include <ExtraData/ExtraDataList.h>
#include <ExtraData/ExtraCharge.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraEnchantment.h>
#include <ExtraData/ExtraHealth.h>
#include <ExtraData/ExtraPoison.h>
#include <ExtraData/ExtraSoul.h>
#include <ExtraData/ExtraTextDisplayData.h>
#include <ExtraData/ExtraWorn.h>
#include <ExtraData/ExtraWornLeft.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/AlchemyItem.h>
#include <core/ItemIds.h>
#include <EquipManager.h>
#include <DefaultObjectManager.h>
#include <BSAnimationGraphManager.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbVariableValueSet.h>
#include <Havok/hkbStateMachine.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESObjectACTI.h>
#include <ExtraData/ExtraFlags.h>
#include <Forms/BGSAction.h>
#include <Animation/TESActionData.h>
#include <Forms/TESWorldSpace.h>

#include <Structs/AnimationGraphDescriptorManager.h>
#include <Structs/AnimationVariables.h>

using TAddInventoryItem = void(TESObjectREFR*, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner);

using TRemoveInventoryItem = BSPointerHandle<TESObjectREFR>*(TESObjectREFR*, BSPointerHandle<TESObjectREFR>* apResult, TESBoundObject* apItem, int32_t aCount, ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc, const NiPoint3* apRotate);

// InventoryChanges::RemoveItem (16059, CommonLibSSE-NG): apRef is the inventory's owner, apDropLoc a reference.
using TRemoveFromInventory = BSPointerHandle<TESObjectREFR>*(ExtraContainerChanges::Data*, BSPointerHandle<TESObjectREFR>* apResult, TESObjectREFR* apRef, TESBoundObject* apItem, int32_t aCount,
                                                            ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc, TESObjectREFR* apDropRef);

static TAddInventoryItem* RealAddInventoryItem = nullptr;

static TRemoveInventoryItem* RealRemoveInventoryItem = nullptr;

static TRemoveFromInventory* RealRemoveFromInventory = nullptr;

void HookAddInventoryItem(TESObjectREFR* apThis, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {

        Inventory::Entry item{};
        item.BaseId = ItemId(apItem->formID).value_or(GameId{});
        item.Count = aCount;

        if (apExtraData)
            apThis->GetItemFromExtraData(item, apExtraData);

        InventoryChangeEvent event(apThis->formID, std::move(item));
        event.StoryGiven = GuestScripts::StoryGiving();
        event.AliasHeld = apExtraData && apExtraData->Contains(ExtraDataType::AliasInstanceArray);
        World::Get().GetRunner().Trigger(std::move(event));
    }

    spdlog::debug("Adding inventory item {:X} to {:X}", apItem->formID, apThis->formID);

    RealAddInventoryItem(apThis, apItem, apExtraData, aCount, apOldOwner);
}

// An item leaving aRef's inventory, as InventoryService sends it.
void SendRemoval(TESObjectREFR* apRef, TESBoundObject* apItem, int32_t aCount, ExtraDataList* apExtraList) noexcept
{
    Inventory::Entry item{};
    item.BaseId = ItemId(apItem->formID).value_or(GameId{});
    if (apExtraList)
    {
        ScopedExtraDataOverride _;
        apRef->GetItemFromExtraData(item, apExtraList);
    }
    item.Count = -aCount;
    World::Get().GetRunner().Trigger(InventoryChangeEvent(apRef->formID, std::move(item)));
}

static std::atomic<bool> s_movesContainerItems{true};

bool ProposeContainerTransfer(TESObjectREFR* apSource, TESObjectREFR* apTarget, TESBoundObject* apItem,
                              int32_t aCount, ExtraDataList* apExtra, ITEM_REMOVE_REASON aReason)
{
    auto* pUI = UI::Get();
    if (s_movesContainerItems.load(std::memory_order_relaxed) || ScopedInventoryOverride::IsOverriden() || !apSource ||
        !apTarget || !apItem || !pUI || !pUI->GetMenuOpen("ContainerMenu"))
        return false;
    Inventory::Entry item;
    item.BaseId = ItemId(apItem->formID).value_or(GameId{});
    if (apExtra)
        TESObjectREFR::GetItemFromExtraData(item, apExtra);
    item.Count = aCount;
    return ContainerTransfers::Propose(World::Get(), apSource->formID, apTarget->formID, std::move(item),
                                      aReason == ITEM_REMOVE_REASON::kSteal);
}

BSPointerHandle<TESObjectREFR>*
HookRemoveInventoryItem(TESObjectREFR* apThis, BSPointerHandle<TESObjectREFR>* apResult, TESBoundObject* apItem, int32_t aCount, ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc, const NiPoint3* apRotate)
{
    if (ProposeContainerTransfer(apThis, apMoveToRef, apItem, aCount, apExtraList, aReason))
    {
        *apResult = {};
        return apResult;
    }
    if (!ScopedInventoryOverride::IsOverriden())
        SendRemoval(apThis, apItem, aCount, apExtraList);

    spdlog::debug("Removing inventory item {:X} from {:X}", apItem->formID, apThis->formID);

    ScopedItemRemovalOverride _;

    return RealRemoveInventoryItem(apThis, apResult, apItem, aCount, aReason, apExtraList, apMoveToRef, apDropLoc, apRotate);
}

// RemoveItem's unequip of a worn stack for an owner with no process (19694): it may free the stack's list (16067)
// and always answers 1. The freed list is remembered for the RemoveItem that follows.
using TUnequipForRemove = bool(TESObjectREFR*, TESBoundObject* apItem, uint32_t aCount, ExtraDataList* apList);

static TUnequipForRemove* RealUnequipForRemove = nullptr;

static thread_local FreedWornList s_freedWornList;

bool HookUnequipForRemove(TESObjectREFR* apThis, TESBoundObject* apItem, uint32_t aCount, ExtraDataList* apList)
{
    const bool cResult = RealUnequipForRemove(apThis, apItem, aCount, apList);
    std::vector<const void*> held;
    if (const auto* pChanges = apThis ? apThis->GetContainerChanges() : nullptr; pChanges && pChanges->entries)
        for (auto* pEntry : *pChanges->entries)
            if (pEntry && pEntry->form == apItem && pEntry->dataList)
                for (ExtraDataList* pList : *pEntry->dataList)
                    held.push_back(pList);
    s_freedWornList.Unequipped(apList, held);
    return cResult;
}

// A removal the engine makes on the inventory itself, not through TESObjectREFR::RemoveItem (the Training Menu's and a
// fine's gold, PayGoldToContainer 37511; a trapped soul's gem): sent as that one is.
BSPointerHandle<TESObjectREFR>* HookRemoveFromInventory(ExtraContainerChanges::Data* apThis, BSPointerHandle<TESObjectREFR>* apResult, TESObjectREFR* apRef, TESBoundObject* apItem,
                                                        int32_t aCount, ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc,
                                                        TESObjectREFR* apDropRef)
{
    struct Depth
    {
        Depth() noexcept { s_freedWornList.Enter(); }
        ~Depth() { s_freedWornList.Leave(); }
    } depth;
    // The list 19694's unequip just freed: taken from no stack, not freed again.
    if (s_freedWornList.IsFreed(apExtraList))
        apExtraList = nullptr;
    TESObjectREFR* pOwner = apRef ? apRef : apThis ? apThis->parent : nullptr;
    if (!ScopedItemRemovalOverride::IsOverriden() &&
        ProposeContainerTransfer(pOwner, apMoveToRef, apItem, aCount, apExtraList, aReason))
    {
        *apResult = {};
        return apResult;
    }
    if (pOwner && apItem && aCount > 0 && !ScopedInventoryOverride::IsOverriden() && !ScopedItemRemovalOverride::IsOverriden())
        SendRemoval(pOwner, apItem, aCount, apExtraList);
    const ItemMenuWatch::Change cChange(pOwner); // it may free a list an open item menu lists
    return RealRemoveFromInventory(apThis, apResult, apRef, apItem, aCount, aReason, apExtraList, apMoveToRef, apDropLoc, apDropRef);
}

// TESObjectREFR::PickLeveledBase (20232, our name): at its cell's load, a reference placed with ExtraLeveledItemBase rolls
// the list, takes the pick as its base (19785) and records it (0x80).
static std::atomic<bool> s_picksLeveledBases{true};

using TPickLeveledBase = void*(TESObjectREFR*);

static TPickLeveledBase* RealPickLeveledBase = nullptr;

void* HookPickLeveledBase(TESObjectREFR* apThis)
{
    if (!s_picksLeveledBases.load(std::memory_order_relaxed))
        return nullptr;
    return RealPickLeveledBase(apThis);
}

void TESObjectREFR::SetPicksLeveledBases(bool aPicks) noexcept
{
    s_picksLeveledBases.store(aPicks, std::memory_order_relaxed);
}

void TESObjectREFR::SetMovesContainerItems(bool aMoves) noexcept
{
    s_movesContainerItems.store(aMoves, std::memory_order_relaxed);
}

// Tempering (InventoryChanges 16145) and enchanting (EnchantObject 16146) change one of a carried stack in place, past
// our add/remove hooks: the owner's stacks of the item before and after, the difference raised as item changes
// (ItemChanges::Diff; party-items.md, re/systems/inventory-changes.md).
using TTemper = void*(ExtraContainerChanges::Data*, TESBoundObject*, ExtraDataList*, float);

using TEnchantObject = ExtraDataList*(ExtraContainerChanges::Data*, TESBoundObject*, ExtraDataList*, EnchantmentItem*, uint16_t);

static TTemper* RealTemper = nullptr;

static TEnchantObject* RealEnchantObject = nullptr;

template <class TCall> static auto ImprovesStack(ExtraContainerChanges::Data* apChanges, TESBoundObject* apObject, TCall&& aCall)
{
    TESObjectREFR* pOwner = apChanges && apObject ? apChanges->parent : nullptr;
    const auto cStacks = [&] { return pOwner->GetInventory([apObject](TESForm& aForm) { return &aForm == apObject; }).Entries; };
    const auto cBefore = pOwner ? cStacks() : std::vector<Inventory::Entry>{};
    auto result = aCall();
    if (pOwner && !ScopedInventoryOverride::IsOverriden())
        for (auto& change : ItemChanges::Diff(cBefore, cStacks()))
            World::Get().GetRunner().Trigger(InventoryChangeEvent(pOwner->formID, std::move(change)));
    return result;
}

static void* HookTemper(ExtraContainerChanges::Data* apChanges, TESBoundObject* apObject, ExtraDataList* apExtra, float aHealth)
{
    return ImprovesStack(apChanges, apObject, [&] { return RealTemper(apChanges, apObject, apExtra, aHealth); });
}

static ExtraDataList* HookEnchantObject(ExtraContainerChanges::Data* apChanges, TESBoundObject* apObject, ExtraDataList* apExtra,
                                        EnchantmentItem* apEnchantment, uint16_t aCharge)
{
    return ImprovesStack(apChanges, apObject, [&] { return RealEnchantObject(apChanges, apObject, apExtra, apEnchantment, aCharge); });
}

static Hooks::Initializer s_objectReferenceItemHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(19708, &RealAddInventoryItem, HookAddInventoryItem);
        HOOK_ID(16145, &RealTemper, HookTemper);
        HOOK_ID(16146, &RealEnchantObject, HookEnchantObject);
        HOOK_ID(19689, &RealRemoveInventoryItem, HookRemoveInventoryItem);
        HOOK_ID(16059, &RealRemoveFromInventory, HookRemoveFromInventory);
        HOOK_ID(19694, &RealUnequipForRemove, HookUnequipForRemove);
        HOOK_ID(20232, &RealPickLeveledBase, HookPickLeveledBase);
    });
