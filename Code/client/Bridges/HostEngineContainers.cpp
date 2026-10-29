#include <Actor.h>
#include <Bridges/HostEngine.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <FormIds.h>
#include <Games/Overrides.h>
#include <Messages/ContainerTransferRequest.h>
#include <RTTI.h>
#include <Services/ContainerTransfers.h>
#include <SessionIds.h>
#include <TESObjectREFR.h>

namespace
{
ExtraDataList* FindStack(TESObjectREFR& aSource, TESBoundObject& aItem, const Inventory::Entry& acWanted,
                         int32_t& aCount)
{
    const auto* pChanges = aSource.FindContainerChanges();
    if (!pChanges || !pChanges->entries)
        return nullptr;
    for (auto* pEntry : *pChanges->entries)
        if (pEntry && pEntry->form == &aItem && pEntry->dataList)
            for (auto* pExtra : *pEntry->dataList)
            {
                if (!pExtra)
                    continue;
                Inventory::Entry item;
                item.BaseId = acWanted.BaseId;
                item.Count = 1;
                TESObjectREFR::GetItemFromExtraData(item, pExtra);
                if (ContainerTransfers::Stack(item).CanBeMerged(acWanted) && item.Count > 0)
                {
                    aCount = std::min(aCount, item.Count);
                    return pExtra;
                }
            }
    return nullptr;
}
} // namespace

Inventory::Entry HostEngine::TransferContainer(const ContainerTransferRequest& acRequest, uint32_t aPlayer) noexcept
{
    Inventory::Entry moved = ContainerTransfers::Stack(acRequest.Item);
    moved.Count = 0;
    auto* pContainer = Cast<TESObjectREFR>(
        TESForm::GetById(acRequest.ContainerActorId ? Utils::GetFormIdBySessionId(acRequest.ContainerActorId)
                                                    : ToGame(acRequest.Container)));
    auto* pPlayer = Cast<Actor>(TESForm::GetById(Utils::GetFormIdBySessionId(aPlayer)));
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(ToGame(moved.BaseId)));
    if (!pContainer || !pPlayer || !pItem || !pContainer->GetContainer())
        return moved;
    const bool cTake = acRequest.Item.Count > 0;
    TESObjectREFR* pSource = cTake ? pContainer : pPlayer;
    TESObjectREFR* pTarget = cTake ? pPlayer : pContainer;
    const auto inventory = pSource->GetInventory([pItem](TESForm& aForm) { return &aForm == pItem; });
    int32_t available = 0;
    for (auto entry : inventory.Entries)
        if (ContainerTransfers::Stack(entry).CanBeMerged(moved) && entry.Count > 0)
            available += entry.Count;
    int32_t remaining = std::min(std::abs(acRequest.Item.Count), available);
    ScopedInventoryOverride _;
    while (remaining > 0)
    {
        int32_t chunk = remaining;
        ExtraDataList* pExtra = moved.ContainsExtraData() ? FindStack(*pSource, *pItem, moved, chunk) : nullptr;
        if (moved.ContainsExtraData() && !pExtra)
            break;
        const int32_t cBefore = pSource->GetContainerCount(pItem).value_or(0);
        pSource->RemoveItem(
            pItem, chunk, acRequest.Steal && cTake ? ITEM_REMOVE_REASON::kSteal : ITEM_REMOVE_REASON::kStoreInContainer,
            pExtra, pTarget);
        const int32_t cTaken = std::clamp(cBefore - pSource->GetContainerCount(pItem).value_or(cBefore), 0, chunk);
        if (!cTaken)
            break;
        remaining -= cTaken;
        moved.Count += cTaken * (cTake ? 1 : -1);
    }
    // The container menu may be showing a stale stack even when nothing was granted.
    if (!acRequest.ContainerActorId)
        RecordItems(pContainer->formID, pItem->formID);
    return moved;
}
