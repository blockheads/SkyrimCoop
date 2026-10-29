#include <Bridges/ContainerItems.h>

#include <FormIds.h>
#include <ItemIds.h>

#include <Actor.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <Games/Overrides.h>
#include <TESObjectREFR.h>

#include <algorithm>
#include <RTTI.h>

std::optional<ObjectChange> ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept
{
    if (IsCreatedForm(aObjectFormId))
        return std::nullopt;
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId));
    if (!pObject || Cast<Actor>(pObject))
        return std::nullopt;
    const auto cCount = pObject->GetContainerCount(Cast<TESBoundObject>(TESForm::GetById(aItemFormId)));
    const auto cObject = ToSession(aObjectFormId);
    const auto cItem = ItemId(aItemFormId);
    if (!cCount || !cObject || !cItem)
        return std::nullopt;
    ObjectChange change{ObjectChange::Kind::kItems, *cObject};
    change.Item = *cItem;
    change.Value = static_cast<uint32_t>(*cCount);
    if (auto* pItem = Cast<TESBoundObject>(TESForm::GetById(aItemFormId)))
        change.Stacks = ReadContainerStacks(*pObject, *pItem);
    return change;
}

std::optional<ObjectChange> ReadContainerContents(const TESObjectREFR& acObject) noexcept
{
    const auto cObject = ToSessionOrCreated(acObject.formID);
    if (acObject.formType != FormType::Reference || !acObject.GetContainer() || !cObject)
        return std::nullopt;
    ObjectChange change{ObjectChange::Kind::kContents, *cObject};
    std::vector<ObjectChange::ItemCount> items;
    for (const auto& [pItem, cCount] : acObject.GetContainerItems())
    {
        if (const auto cItem = ItemId(pItem->formID); cCount && cItem)
        {
            items.push_back({*cItem, cCount});
            std::ranges::move(ReadContainerStacks(acObject, *pItem), std::back_inserter(change.Stacks));
        }
    }
    change.Data = ObjectChange::PackContents(items);
    return change;
}

bool ApplyContainerContents(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (aObject.formType != FormType::Reference || !aObject.GetContainer())
        return false;
    std::vector<std::pair<TESBoundObject*, int32_t>> wanted;
    for (const auto& cItem : ObjectChange::UnpackContents(acChange.Data))
        if (auto* pItem = Cast<TESBoundObject>(TESForm::GetById(ItemForm(cItem.Item))))
            wanted.emplace_back(pItem, cItem.Count);
    for (const auto& [pItem, cCount] : aObject.GetContainerItems())
        if (std::ranges::none_of(wanted, [pItem](const auto& acWanted) { return acWanted.first == pItem; }))
            aObject.SetContainerCount(pItem, 0);
    for (const auto& [pItem, cCount] : wanted)
    {
        std::vector<Inventory::Entry> stacks;
        for (const auto& cStack : acChange.Stacks)
            if (cStack.BaseId == ItemId(pItem->formID))
                stacks.push_back(cStack);
        SetContainerStacks(aObject, *pItem, cCount, stacks);
    }
    return true;
}

namespace
{
// The item's entry in the container's changes, none for no changes of it.
ExtraContainerChanges::Entry* FindEntry(const TESObjectREFR& acObject, const TESBoundObject& acItem) noexcept
{
    const auto* pChanges = acObject.FindContainerChanges();
    if (!pChanges || !pChanges->entries)
        return nullptr;
    for (auto* pEntry : *pChanges->entries)
        if (pEntry && pEntry->form == &acItem)
            return pEntry;
    return nullptr;
}

// A stack's item extra data: worn and quest flags are its holder's, not the item's.
std::optional<Inventory::Entry> ReadStack(const TESBoundObject& acItem, ExtraDataList* apList) noexcept
{
    Inventory::Entry stack;
    stack.BaseId = ItemId(acItem.formID).value_or(GameId{});
    stack.Count = 1;
    TESObjectREFR::GetItemFromExtraData(stack, apList);
    stack.ExtraWorn = stack.ExtraWornLeft = stack.IsQuestItem = false;
    if (!stack.ContainsExtraData() || stack.Count <= 0)
        return std::nullopt;
    return stack;
}

// The same stacks, in any order.
bool SameStacks(std::vector<Inventory::Entry> aHave, const std::vector<Inventory::Entry>& acWanted) noexcept
{
    for (const auto& cWanted : acWanted)
    {
        const auto it = std::ranges::find(aHave, cWanted);
        if (it == aHave.end())
            return false;
        aHave.erase(it);
    }
    return aHave.empty();
}
} // namespace

std::vector<Inventory::Entry> ReadContainerStacks(const TESObjectREFR& acObject, TESBoundObject& aItem) noexcept
{
    std::vector<Inventory::Entry> stacks;
    const auto* pEntry = FindEntry(acObject, aItem);
    if (!pEntry || !pEntry->dataList)
        return stacks;
    for (ExtraDataList* pList : *pEntry->dataList)
    {
        auto stack = pList ? ReadStack(aItem, pList) : std::nullopt;
        if (!stack)
            continue;
        if (const auto it = std::ranges::find_if(stacks, [&](const auto& acStack) { return acStack.CanBeMerged(*stack); }); it != stacks.end())
            it->Count += stack->Count;
        else
            stacks.push_back(std::move(*stack));
    }
    return stacks;
}

bool SetContainerStacks(TESObjectREFR& aObject, TESBoundObject& aItem, int32_t aCount, const std::vector<Inventory::Entry>& acStacks) noexcept
{
    if (!aObject.GetContainer())
        return false;
    if (!SameStacks(ReadContainerStacks(aObject, aItem), acStacks))
    {
        ScopedInventoryOverride _;
        // Each stack with extra data out (removing one frees its list: found again each time), then the wanted ones in.
        const ExtraDataList* pLast = nullptr;
        for (int removed = 0; removed < 256; ++removed)
        {
            const auto* pEntry = FindEntry(aObject, aItem);
            ExtraDataList* pFound = nullptr;
            int32_t count = 0;
            if (pEntry && pEntry->dataList)
                for (ExtraDataList* pList : *pEntry->dataList)
                    if (const auto cStack = pList ? ReadStack(aItem, pList) : std::nullopt)
                    {
                        pFound = pList;
                        count = cStack->Count;
                        break;
                    }
            // None left, or one the engine would not remove.
            if (!pFound || pFound == pLast)
                break;
            pLast = pFound;
            aObject.RemoveItem(&aItem, count, ITEM_REMOVE_REASON::kRemove, pFound, nullptr);
        }
        for (const auto& cStack : acStacks)
            if (cStack.Count > 0)
                aObject.AddObjectToContainer(&aItem, TESObjectREFR::GetExtraDataFromItem(cStack, true), cStack.Count, nullptr);
    }
    aObject.SetContainerCount(&aItem, aCount);
    return true;
}

bool ApplyContainerItems(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(ItemForm(acChange.Item)));
    return pItem && !Cast<Actor>(&aObject) && SetContainerStacks(aObject, *pItem, static_cast<int32_t>(acChange.Value), acChange.Stacks);
}
