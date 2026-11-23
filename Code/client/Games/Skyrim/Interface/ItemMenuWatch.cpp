#include <Interface/ItemMenuWatch.h>

#include <Games/ItemMenuRebuild.h>
#include <Games/TES.h>
#include <Interface/Menus/ItemMenus.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <TESObjectREFR.h>

#include <array>
#include <atomic>

namespace ItemMenuWatch
{
namespace
{
std::array<std::atomic<IMenu*>, 3> s_open{};

// The menu's list and the holders it lists (ItemMenuRebuild.h), read from the engine.
std::pair<ItemList*, ItemMenuView> Read(Kind aKind, IMenu* apMenu, uint32_t aPlayer) noexcept
{
    ItemList* pList = nullptr;
    uint32_t holder = 0;
    switch (aKind)
    {
    case Kind::Container:
    {
        pList = static_cast<ContainerMenu*>(apMenu)->pItemList;
        POINTER_SKYRIMSE(uint32_t, s_target, 405962); // ContainerMenu's target (CommonLibSSE-NG GetTargetRefHandle)
        const auto* pTarget = TESObjectREFR::GetByHandle(*s_target.Get());
        holder = pTarget ? pTarget->formID : 0;
        break;
    }
    case Kind::Barter:
    {
        pList = static_cast<BarterMenu*>(apMenu)->pItemList;
        const auto* pDataHandler = ModManager::Get();
        const auto* pStock = pDataHandler ? pDataHandler->merchantInventory : nullptr;
        holder = pStock && pStock->parent ? pStock->parent->formID : 0;
        break;
    }
    case Kind::Inventory: pList = static_cast<InventoryMenu*>(apMenu)->pItemList; break;
    }
    return {pList, ItemMenuView{.Open = pList != nullptr, .Lists = {holder, aPlayer}}};
}
} // namespace

void Shown(Kind aKind, IMenu* apMenu) noexcept
{
    s_open[static_cast<size_t>(aKind)].store(apMenu, std::memory_order_release);
}

void Hidden(Kind aKind) noexcept
{
    s_open[static_cast<size_t>(aKind)].store(nullptr, std::memory_order_release);
}

void RebuildFirst(const TESObjectREFR* apHolder, const TESBoundObject* apItem) noexcept
{
    if (!apHolder)
        return;
    auto* pPlayer = PlayerCharacter::Get();
    const uint32_t cPlayer = pPlayer ? pPlayer->formID : 0;
    std::array<ItemList*, 3> lists{};
    bool listed = false;
    for (size_t i = 0; i < s_open.size(); ++i)
        if (IMenu* pMenu = s_open[i].load(std::memory_order_acquire))
        {
            const auto [pList, cView] = Read(static_cast<Kind>(i), pMenu, cPlayer);
            if (MustRebuildFirst(cView, apHolder->formID))
                lists[i] = pList, listed = true;
        }
    if (!listed)
        return;
    // The player's handle: every item menu's kInventoryUpdate takes it (403520; 51126, 50945, 51848). First each item
    // into the menu's changed set (none given: all it holds), then Update_Impl, whose rebuild re-reads that set (51146).
    using TSend = void(TESObjectREFR*, const TESBoundObject*);
    POINTER_SKYRIMSE(TSend, s_send, 52849);
    if (apItem)
        s_send.Get()(pPlayer, apItem);
    else if (const auto* pChanges = apHolder->FindContainerChanges(); pChanges && pChanges->entries)
        for (const auto* pEntry : *pChanges->entries)
            if (const auto* pItem = pEntry ? Cast<TESBoundObject>(pEntry->form) : nullptr)
                s_send.Get()(pPlayer, pItem);
    using TUpdate = void(ItemList*, TESObjectREFR*);
    POINTER_SKYRIMSE(TUpdate, s_update, 51031);
    for (ItemList* pList : lists)
        if (pList)
            s_update.Get()(pList, pPlayer);
}
} // namespace ItemMenuWatch
