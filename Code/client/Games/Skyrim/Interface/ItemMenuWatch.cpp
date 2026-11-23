#include <Interface/ItemMenuWatch.h>

#include <Games/ItemMenuRebuild.h>
#include <Games/TES.h>
#include <Interface/Menus/ItemMenus.h>
#include <Interface/UI.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>

#include <array>
#include <atomic>

namespace ItemMenuWatch
{
namespace
{
constexpr std::array<const char*, ItemMenuChanges::kMenus> cNames{"ContainerMenu", "BarterMenu", "InventoryMenu"};
std::array<std::atomic<IMenu*>, ItemMenuChanges::kMenus> s_open{};
// The holder each open menu lists beside the player, read on the UI's thread for the other threads' Note.
std::array<std::atomic<uint32_t>, ItemMenuChanges::kMenus> s_holder{};
std::atomic<uint32_t> s_player{};
ItemMenuChanges s_changes;

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
    s_holder[static_cast<size_t>(aKind)].store(ListedHolder(aKind), std::memory_order_release);
    s_open[static_cast<size_t>(aKind)].store(apMenu, std::memory_order_release);
}

void Hidden(Kind aKind) noexcept
{
    s_open[static_cast<size_t>(aKind)].store(nullptr, std::memory_order_release);
}

void Note(const TESObjectREFR* apHolder) noexcept
{
    if (!apHolder)
        return;
    for (size_t i = 0; i < s_open.size(); ++i)
        if (s_open[i].load(std::memory_order_acquire))
        {
            const ItemMenuView cView{.Open = true, .Lists = {s_holder[i].load(std::memory_order_acquire), s_player.load(std::memory_order_acquire)}};
            if (MustRebuildFirst(cView, apHolder->formID))
                s_changes.Note(i);
        }
}

void BeforeUiMessages() noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    s_player.store(pPlayer ? pPlayer->formID : 0, std::memory_order_release);
    UI* pUI = UI::Get();
    for (size_t i = 0; i < s_open.size(); ++i)
    {
        const auto cKind = static_cast<Kind>(i);
        IMenu* pMenu = s_open[i].load(std::memory_order_acquire);
        // Still the UI's menu of that name: one gone without a hide message is dropped, never read.
        if (pMenu && (!pUI || pUI->FindMenuByName(cNames[i]) != pMenu))
        {
            Hidden(cKind);
            pMenu = nullptr;
        }
        if (pMenu)
            s_holder[i].store(ListedHolder(cKind), std::memory_order_release);
        if (!s_changes.Take(i, pMenu != nullptr) || !pPlayer)
            continue;
        const auto [pList, pChanged] = ListOf(cKind, pMenu);
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
