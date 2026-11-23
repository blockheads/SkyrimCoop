#pragma once

#include <array>
#include <cstdint>

// An open item menu (ContainerMenu, BarterMenu, InventoryMenu) lists its holders' stacks by their own extra data lists
// (16106 at 0x14023929d). Before a stack of one of them changes, as the engine's own take does: the item's update
// (52849, into the menu's changed set) then ItemList::Update_Impl (51031: the selection hidden, the set re-read), even
// when a rebuild is pending already (docs/re/systems/item-menus.md).
struct ItemMenuView
{
    bool Open{};
    // ContainerMenu: its target (405962) and the player; BarterMenu: the merchant inventory's owner (TESDataHandler
    // +0xDB8) and the player; InventoryMenu: the player.
    std::array<uint32_t, 2> Lists{};
};

[[nodiscard]] constexpr bool MustRebuildFirst(const ItemMenuView& acMenu, uint32_t aHolder) noexcept
{
    return acMenu.Open && aHolder && (aHolder == acMenu.Lists[0] || aHolder == acMenu.Lists[1]);
}
