#include <Actor.h>
#include <Bridges/HostEngine.h>
#include <items/LooseItems.h>
#include <items/ItemsChangedEvent.h>
#include <Forms/TESObjectCELL.h>
#include <core/World.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <ExtraData/ExtraDataList.h>
#include <core/FormIds.h>
#include <core/ItemIds.h>
#include <items/Overrides.h>
#include <items/PartyItems.h>
#include <Messages/ContainerTransferRequest.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <items/ContainerTransfers.h>
#include <core/SessionIds.h>
#include <TESObjectREFR.h>

namespace
{
ExtraDataList* FindStack(TESObjectREFR& aSource, TESBoundObject& aItem, const Inventory::Entry& acWanted,
                         int32_t& aCount, bool aAliasOnly = false)
{
    const auto* pChanges = aSource.FindContainerChanges();
    if (!pChanges || !pChanges->entries)
        return nullptr;
    for (auto* pEntry : *pChanges->entries)
        if (pEntry && pEntry->form == &aItem && pEntry->dataList)
            for (auto* pExtra : *pEntry->dataList)
            {
                if (!pExtra || (aAliasOnly && !pExtra->Contains(ExtraDataType::AliasInstanceArray)))
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

Inventory::Entry HostEngine::MoveAliasStack(TESObjectREFR& aContainer, TESBoundObject& aItem, ExtraDataList& aList, int32_t aCount,
                                            const ContainerTransferRequest& acRequest) noexcept
{
    // Moved with a target (16059), the alias keeps it and its script hears the new container: this game's player, where
    // the party holds it (inventory-changes.md, "An alias-held item leaving a container"; shared-items.md).
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    Inventory::Entry moved = ContainerTransfers::Stack(acRequest.Item);
    moved.Count = 0;
    if (!pPlayer)
        return moved;
    const int32_t cBefore = aContainer.GetContainerCount(&aItem).value_or(0);
    // kSteal stamps the source's owner on the moved list (16059 with a target): a pickpocket's item stays stolen.
    aContainer.RemoveItem(&aItem, aCount, acRequest.Steal ? ITEM_REMOVE_REASON::kSteal : ITEM_REMOVE_REASON::kStoreInContainer, &aList, pPlayer);
    moved.Count = std::clamp(cBefore - aContainer.GetContainerCount(&aItem).value_or(cBefore), 0, aCount);
    moved.IsQuestItem = true; // HostItems: the host's player holds it
    spdlog::info("HostEngine: take {} of {:X} from {:X}: an alias holds it, moved to {:X}", moved.Count, aItem.formID, aContainer.formID, pPlayer->formID);
    if (!acRequest.ContainerActorId)
        RecordItems(aContainer.formID, aItem.formID);
    else if (moved.Count)
        World::Runner().Trigger(ItemsChangedEvent{aContainer.formID});
    return moved;
}

std::optional<GameId> HostEngine::DropPartyItem(const Inventory::Entry& acItem, uint32_t aPlayer) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    auto* pCopy = Cast<Actor>(TESForm::GetById(Utils::GetFormIdBySessionId(aPlayer)));
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(ItemForm(acItem.BaseId)));
    if (!pPlayer || !pCopy || !pItem)
        return std::nullopt;
    Inventory::Entry wanted = ContainerTransfers::Stack(acItem);
    int32_t count = std::abs(acItem.Count);
    ExtraDataList* pList = FindStack(*pPlayer, *pItem, wanted, count);
    if (!pList)
    {
        // A quest object's real stack an alias names (untagged): refused, as Skyrim refuses its drop.
        wanted.ExtraName.clear();
        count = std::abs(acItem.Count);
        ExtraDataList* pQuest = FindStack(*pPlayer, *pItem, wanted, count);
        return pQuest && pQuest->HasQuestObjectAlias() ? std::optional(GameId{}) : std::nullopt;
    }
    // PlaceDropped (16062) puts a drop in the dropper's own cell: elsewhere an alias's stack is refused, not lost.
    const TESObjectCELL* pCell = pPlayer->GetParentCell();
    const TESObjectCELL* pCopyCell = pCopy->GetParentCell();
    const bool cAliasHeld = pList->Contains(ExtraDataType::AliasInstanceArray);
    if (pList->HasQuestObjectAlias())
        return GameId{};
    if (!pCell || !pCopyCell || (pCell != pCopyCell && (!pCell->worldspace || pCell->worldspace != pCopyCell->worldspace)))
        return cAliasHeld ? std::optional(GameId{}) : std::nullopt;
    const NiPoint3 cPosition = pCopy->position;
    const NiPoint3 cRotation = pCopy->rotation;
    ScopedInventoryOverride _;
    const auto cHandle = pPlayer->RemoveItem(pItem, count, ITEM_REMOVE_REASON::kDropping, pList, nullptr, &cPosition, &cRotation);
    const TESObjectREFR* pDropped = TESObjectREFR::GetByHandle(cHandle.handle.iBits);
    if (!pDropped || !pDropped->IsTemporary())
    {
        spdlog::warn("HostEngine: the party's {:X} dropped for {:X} made no created reference", pItem->formID, pCopy->formID);
        return std::nullopt;
    }
    // The engine's drop passes our watch of its creations (measured): it goes out with them.
    spdlog::info("HostEngine: the party's {:X} dropped where {:X} stands: {:X}", pItem->formID, pCopy->formID, pDropped->formID);
    return CreatedId(pDropped->formID);
}

Inventory::Entry HostEngine::TransferContainer(const ContainerTransferRequest& acRequest, uint32_t aPlayer) noexcept
{
    Inventory::Entry moved = ContainerTransfers::Stack(acRequest.Item);
    moved.Count = 0;
    auto* pContainer = Cast<TESObjectREFR>(
        TESForm::GetById(acRequest.ContainerActorId ? Utils::GetFormIdBySessionId(acRequest.ContainerActorId)
                                                    : ToGame(acRequest.Container)));
    auto* pPlayer = Cast<Actor>(TESForm::GetById(Utils::GetFormIdBySessionId(aPlayer)));
    auto* pItem = Cast<TESBoundObject>(TESForm::GetById(ItemForm(moved.BaseId)));
    if (!pContainer || !pPlayer || !pItem || !pContainer->GetContainer())
        return moved;
    const bool cTake = acRequest.Item.Count > 0;
    TESObjectREFR* pSource = cTake ? pContainer : pPlayer;
    const auto inventory = pSource->GetInventory([pItem](TESForm& aForm) { return &aForm == pItem; });
    int32_t available = 0;
    if (moved.ContainsExtraData())
    {
        for (auto entry : inventory.Entries)
            if (ContainerTransfers::Stack(entry).CanBeMerged(moved) && entry.Count > 0)
                available += entry.Count;
    }
    else
    {
        // The engine's count (16108): a fresh roll's items count through the base's leveled list, not the changes'.
        available = pSource->GetContainerCount(pItem).value_or(0);
        for (auto entry : inventory.Entries)
            if (ContainerTransfers::Stack(entry).ContainsExtraData() && entry.Count > 0)
                available -= entry.Count;
    }
    int32_t remaining = std::min(std::abs(acRequest.Item.Count), available);
    int32_t aliasCount = remaining;
    if (cTake && remaining > 0)
        if (ExtraDataList* pAlias = FindStack(*pSource, *pItem, moved, aliasCount, true))
            return MoveAliasStack(*pContainer, *pItem, *pAlias, aliasCount, acRequest);
    ScopedInventoryOverride _;
    // A put of a party item's mirror: the host's player's real stack goes in instead, name and alias with it, or nothing
    // (shared-items.md). IsQuestItem tells HostItems the host's player's own stack moved.
    if (!cTake && PartyItems::IsParty(moved))
        if (PlayerCharacter* pHost = PlayerCharacter::Get())
        {
            int32_t count = std::abs(acRequest.Item.Count);
            ExtraDataList* pReal = FindStack(*pHost, *pItem, moved, count);
            if (!pReal)
            {
                // A quest object's real stack an alias names keeps that name, not the mirror's tag.
                Inventory::Entry quest = moved;
                quest.ExtraName.clear();
                count = std::abs(acRequest.Item.Count);
                pReal = FindStack(*pHost, *pItem, quest, count);
                if (pReal && !pReal->HasQuestObjectAlias())
                    pReal = nullptr;
            }
            moved.Count = 0;
            if (pReal)
            {
                const int32_t cBefore = pHost->GetContainerCount(pItem).value_or(0);
                pHost->RemoveItem(pItem, count, ITEM_REMOVE_REASON::kStoreInContainer, pReal, pContainer);
                moved.Count = -std::clamp(cBefore - pHost->GetContainerCount(pItem).value_or(cBefore), 0, count);
                moved.IsQuestItem = moved.Count != 0;
                spdlog::info("HostEngine: put {} of the party's {:X} into {:X}: the host's player's own", -moved.Count, pItem->formID, pContainer->formID);
                if (!acRequest.ContainerActorId)
                    RecordItems(pContainer->formID, pItem->formID);
                else if (moved.Count)
                    World::Runner().Trigger(ItemsChangedEvent{pContainer->formID});
            }
            return moved;
        }
    // A take only leaves the container: the looter's own game changes its player (one-simulator.md, piece 3). A put
    // moves from the host's copy of the player, its count spent at once against a second put before the player's
    // next items part, and its real list (worn, a roll's) to the container.
    TESObjectREFR* pTarget = cTake ? nullptr : pContainer;
    bool foundList = false;
    while (remaining > 0)
    {
        int32_t chunk = remaining;
        // Plain to us is not plain to the engine: a fresh roll's items sit in lists (ExtraLeveledItem), which 16059
        // takes from only when given the list (S5).
        ExtraDataList* pExtra = FindStack(*pSource, *pItem, moved, chunk);
        foundList = foundList || pExtra;
        if (moved.ContainsExtraData() && !pExtra)
            break;
        const int32_t cBefore = pSource->GetContainerCount(pItem).value_or(0);
        pSource->RemoveItem(pItem, chunk, acRequest.Steal && cTake ? ITEM_REMOVE_REASON::kSteal : ITEM_REMOVE_REASON::kStoreInContainer,
                            pExtra, pTarget);
        const int32_t cTaken = std::clamp(cBefore - pSource->GetContainerCount(pItem).value_or(cBefore), 0, chunk);
        if (!cTaken)
            break;
        remaining -= cTaken;
        moved.Count += cTaken * (cTake ? 1 : -1);
    }
    spdlog::info("HostEngine: {} {} of {:X} {} {:X}: {} there, {} moved{} (asked {}, a list {})", cTake ? "take" : "put",
                 std::abs(acRequest.Item.Count), pItem->formID, cTake ? "from" : "into", pContainer->formID, available, std::abs(moved.Count),
                 acRequest.Steal ? " (theft)" : "", moved.ContainsExtraData() ? "with extra data" : "plain", foundList ? "found" : "not found");
    // A theft marks what it takes as the owner's (16059 does so only on an item given to a target): stolen on the
    // looter's game (inventory-changes.md, "What 16059's target does").
    if (cTake && acRequest.Steal && moved.Count && moved.ExtraOwner == GameId{} && pItem->formID != 0xF)
        if (TESForm* pOwner = pContainer->GetItemsOwner())
            moved.ExtraOwner = ToSession(pOwner->formID).value_or(GameId{});
    // The container menu may be showing a stale stack even when nothing was granted.
    if (!acRequest.ContainerActorId)
        RecordItems(pContainer->formID, pItem->formID);
    else if (moved.Count)
        World::Runner().Trigger(ItemsChangedEvent{pContainer->formID}); // its items part, silent above
    return moved;
}
