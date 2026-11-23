#include <Interface/ItemMenuWatch.h>

#include <Games/ItemMenuRebuild.h>
#include <Games/TES.h>
#include <Interface/Menus/ItemMenus.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>

#include <array>
#include <atomic>

namespace ItemMenuWatch
{
namespace
{
std::array<std::atomic<IMenu*>, ItemMenuChanges::kMenus> s_open{};
ItemMenuChanges s_changes;

// The holder a menu lists beside the player, from the engine's globals (no menu memory: any thread).
uint32_t ListedHolder(Kind aKind) noexcept
{
    switch (aKind)
    {
    case Kind::Container:
    {
        POINTER_SKYRIMSE(uint32_t, s_target, 405962); // ContainerMenu's target (CommonLibSSE-NG GetTargetRefHandle)
        const auto* pTarget = TESObjectREFR::GetByHandle(*s_target.Get());
        return pTarget ? pTarget->formID : 0;
    }
    case Kind::Barter:
    {
        const auto* pDataHandler = ModManager::Get();
        const auto* pStock = pDataHandler ? pDataHandler->merchantInventory : nullptr;
        return pStock && pStock->parent ? pStock->parent->formID : 0;
    }
    case Kind::Inventory: return 0;
    }
    return 0;
}

// The menu's list and the count of items its next rebuild would re-read alone.
std::pair<ItemList*, uint32_t*> ListOf(Kind aKind, IMenu* apMenu) noexcept
{
    switch (aKind)
    {
    case Kind::Container: return {static_cast<ContainerMenu*>(apMenu)->pItemList, &static_cast<ContainerMenu*>(apMenu)->ChangedFormCount};
    case Kind::Barter: return {static_cast<BarterMenu*>(apMenu)->pItemList, &static_cast<BarterMenu*>(apMenu)->ChangedFormCount};
    case Kind::Inventory: return {static_cast<InventoryMenu*>(apMenu)->pItemList, &static_cast<InventoryMenu*>(apMenu)->ChangedFormCount};
    }
    return {};
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

void RebuildFirst(const TESObjectREFR* apHolder) noexcept
{
    if (!apHolder)
        return;
    const auto* pPlayer = PlayerCharacter::Get();
    const uint32_t cPlayer = pPlayer ? pPlayer->formID : 0;
    for (size_t i = 0; i < s_open.size(); ++i)
        if (s_open[i].load(std::memory_order_acquire))
        {
            const ItemMenuView cView{.Open = true, .Lists = {ListedHolder(static_cast<Kind>(i)), cPlayer}};
            if (MustRebuildFirst(cView, apHolder->formID))
                s_changes.Note(i);
        }
}

void OnUiJob() noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    for (size_t i = 0; i < s_open.size(); ++i)
    {
        IMenu* pMenu = s_open[i].load(std::memory_order_acquire);
        if (!s_changes.Take(i, pMenu != nullptr) || !pPlayer)
            continue;
        const auto [pList, pChanged] = ListOf(static_cast<Kind>(i), pMenu);
        if (!pList)
            continue;
        // No items named: the rebuild reads the whole list, not only those (51144 -> 51146, 50958 -> 50960).
        *pChanged = 0;
        // The player as owner: every item menu's kInventoryUpdate takes it (403520; 51126, 50945, 51848).
        using TUpdate = void(ItemList*, TESObjectREFR*);
        POINTER_SKYRIMSE(TUpdate, s_update, 51031);
        s_update.Get()(pList, pPlayer);
    }
}
} // namespace ItemMenuWatch
