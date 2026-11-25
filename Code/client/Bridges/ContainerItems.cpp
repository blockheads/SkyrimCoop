#include <Bridges/ContainerItems.h>

#include <FormIds.h>
#include <ItemIds.h>

#include <Actor.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <ExtraData/ExtraCount.h>
#include <Games/Overrides.h>
#include <Interface/ItemMenuWatch.h>
#include <TESObjectREFR.h>

#include <algorithm>
#include <RTTI.h>
#include <Structs/ItemsPart.h>

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

namespace
{
// An open item menu on it lists what it holds now: the engine announces no change another game made. Its whole list,
// twice: one update can leave removed entries (Skyrim Souls RE's ItemMenuUpdater; SendInventoryUpdateMessage 52849,
// to the top menu, CommonLibSSE-NG).
void ShowInItemMenus(TESObjectREFR& aObject) noexcept
{
    using TSend = void(TESObjectREFR*, const TESBoundObject*);
    POINTER_SKYRIMSE(TSend, s_send, 52849);
    s_send.Get()(&aObject, nullptr);
    s_send.Get()(&aObject, nullptr);
}
} // namespace

bool ApplyContainerContents(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (aObject.formType != FormType::Reference || !aObject.GetContainer())
        return false;
    const ItemMenuWatch::Change cChange(&aObject);
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
    ShowInItemMenus(aObject);
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
    const ItemMenuWatch::Change cChange(&aObject);
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
    if (!pItem || Cast<Actor>(&aObject) || !SetContainerStacks(aObject, *pItem, static_cast<int32_t>(acChange.Value), acChange.Stacks))
        return false;
    ShowInItemMenus(aObject);
    return true;
}

ExtraDataList* FindNamedList(TESObjectREFR& aHolder, TESBoundObject& aItem, const std::string& acName) noexcept
{
    auto* pEntry = FindEntry(aHolder, aItem);
    if (!pEntry || !pEntry->dataList || acName.empty())
        return nullptr;
    for (ExtraDataList* pList : *pEntry->dataList)
        if (const auto cStack = pList ? ReadStack(aItem, pList) : std::nullopt; cStack && cStack->ExtraName == acName)
            return pList;
    return nullptr;
}

int32_t TagStackInPlace(TESObjectREFR& aHolder, TESBoundObject& aItem, const char* acpName, int32_t aCount) noexcept
{
    auto* pEntry = FindEntry(aHolder, aItem);
    if (!pEntry || !pEntry->dataList)
        return 0;
    // An alias's list first, then one that reads plain to us (worn, a favorite): never one already named.
    for (const bool cAliasPass : {true, false})
        for (ExtraDataList* pList : *pEntry->dataList)
        {
            if (!pList || pList->HasQuestObjectAlias() || pList->Contains(ExtraDataType::AliasInstanceArray) != cAliasPass ||
                (!cAliasPass && ReadStack(aItem, pList)))
                continue;
            const auto* pCount = static_cast<const ExtraCount*>(pList->GetByType(ExtraDataType::Count));
            const int32_t cInList = pCount && pCount->count > 0 ? pCount->count : 1;
            if (cInList <= aCount && pList->SetCustomName(acpName))
                return cInList;
        }
    return 0;
}

bool SetCarriedStacks(TESObjectREFR& aActor, TESBoundObject& aItem, int32_t aCount, const std::vector<Inventory::Entry>& acStacks) noexcept
{
    if (!aActor.GetContainer())
        return false;
    const ItemMenuWatch::Change cChange(&aActor);
    ScopedInventoryOverride _;
    std::vector<Inventory::Entry> missing;
    for (Inventory::Entry stack : acStacks)
    {
        stack.ExtraWorn = stack.ExtraWornLeft = stack.IsQuestItem = false;
        if (stack.Count > 0)
            missing.push_back(std::move(stack));
    }
    // Each carried list with extra data keeps what is wanted of its stack; the rest of it goes, by its own list (16059).
    std::vector<std::pair<ExtraDataList*, int32_t>> removals;
    if (auto* pEntry = FindEntry(aActor, aItem); pEntry && pEntry->dataList)
        for (ExtraDataList* pList : *pEntry->dataList)
            if (const auto cStack = pList ? ReadStack(aItem, pList) : std::nullopt)
            {
                const auto it = std::ranges::find_if(missing, [&](const auto& acWanted) { return acWanted.Count > 0 && ItemsPart::SameStack(acWanted, *cStack); });
                const int32_t cKept = it != missing.end() ? std::min(it->Count, cStack->Count) : 0;
                if (it != missing.end())
                    it->Count -= cKept;
                if (cStack->Count > cKept)
                    removals.emplace_back(pList, cStack->Count - cKept);
            }
    for (const auto& [pList, cCount] : removals)
        aActor.RemoveItem(&aItem, cCount, ITEM_REMOVE_REASON::kRemove, pList, nullptr);
    for (const auto& cStack : missing)
        if (cStack.Count > 0)
            aActor.AddObjectToContainer(&aItem, TESObjectREFR::GetExtraDataFromItem(cStack, true), cStack.Count, nullptr);
    aActor.SetContainerCount(&aItem, aCount);
    // A no-list removal takes only what no list holds (16059): the rest through the lists that read as plain (a roll's),
    // each standing for its ExtraCount (none: one), while each pass takes some.
    for (int32_t held = aActor.GetContainerCount(&aItem).value_or(0); held > aCount;)
    {
        ExtraDataList* pPlain = nullptr;
        if (auto* pEntry = FindEntry(aActor, aItem); pEntry && pEntry->dataList)
            for (ExtraDataList* pList : *pEntry->dataList)
                if (pList && !ReadStack(aItem, pList))
                {
                    pPlain = pList;
                    break;
                }
        if (!pPlain)
            break;
        const auto* pCount = static_cast<const ExtraCount*>(pPlain->GetByType(ExtraDataType::Count));
        const int32_t cInList = pCount && pCount->count > 0 ? pCount->count : 1;
        aActor.RemoveItem(&aItem, std::min(held - aCount, cInList), ITEM_REMOVE_REASON::kRemove, pPlain, nullptr);
        const int32_t cNow = aActor.GetContainerCount(&aItem).value_or(0);
        if (cNow >= held)
            break;
        held = cNow;
    }
    return true;
}
