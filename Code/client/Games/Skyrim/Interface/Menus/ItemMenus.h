#pragma once

#include <Interface/IMenu.h>

// CommonLibSSE-NG ItemList: an item menu's native list. Its items' entries hold the listed stacks' own lists (16106 at
// 0x14023929d); UpdatePending, set by Update_Impl (51031), hides the selection until the menu rebuilds it.
struct ItemList
{
    uint8_t pad0[0x50];
    bool UpdatePending;
};

static_assert(offsetof(ItemList, UpdatePending) == 0x50); // read in IDA: ItemList__GetSelectedItem, 51144 at 0x140912e74

// The item menus' lists (CommonLibSSE-NG RUNTIME_DATA at 0x30), as their ProcessMessage reads them (IDA 1.7.104).
struct ContainerMenu : IMenu
{
    uint8_t pad30[0x18];
    ItemList* pItemList; // 51126, 51130, 51132
};

struct BarterMenu : IMenu
{
    ItemList* pItemList; // 50945, 50960
};

struct InventoryMenu : IMenu
{
    uint8_t pad30[0x18];
    ItemList* pItemList; // 51848, RefreshItemList 51866
};

static_assert(offsetof(ContainerMenu, pItemList) == 0x48);
static_assert(offsetof(BarterMenu, pItemList) == 0x30);
static_assert(offsetof(InventoryMenu, pItemList) == 0x48);
