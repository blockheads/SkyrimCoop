#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// An open item menu (ContainerMenu, BarterMenu, InventoryMenu) lists its holders' stacks by their own extra data lists
// (16106 at 0x14023929d). A change to a listed holder is noted on any thread; the UI job (Job_UI 39042) rebuilds the
// menu before its callbacks can read the list (docs/re/systems/item-menus.md).
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

// Each menu's noted change: any thread notes it, the UI job takes it.
class ItemMenuChanges
{
public:
    static constexpr size_t kMenus = 3;

    void Note(size_t aMenu) noexcept { m_changed[aMenu].store(true, std::memory_order_release); }
    // Once per noted change, for a menu still open; a closed menu's is dropped.
    [[nodiscard]] bool Take(size_t aMenu, bool aOpen) noexcept
    {
        return m_changed[aMenu].exchange(false, std::memory_order_acq_rel) && aOpen;
    }

private:
    std::array<std::atomic<bool>, kMenus> m_changed{};
};
