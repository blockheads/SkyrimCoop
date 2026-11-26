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

// The item menus' lists and the items an item update named (a BSTArray: its size), as their ProcessMessage and rebuild
// read them (IDA 1.7.104). A rebuild with named items re-reads only those (51146, 50960); none: the whole list.
struct ContainerMenu : IMenu
{
    uint8_t pad30[0x18];
    ItemList* pItemList; // 51126, 51130, 51132
    uint8_t pad50[0x20];
    uint32_t ChangedFormCount; // 51126 case 8, 51144, 51146
};

struct BarterMenu : IMenu
{
    ItemList* pItemList; // 50945, 50958
    uint8_t pad38[0x58];
    uint32_t ChangedFormCount; // 50945 case 8, 50958
};

struct InventoryMenu : IMenu
{
    uint8_t pad30[0x18];
    ItemList* pItemList; // 51848, RefreshItemList 51866
    uint8_t pad50[0x20];
    uint32_t ChangedFormCount; // 51848 case 8
};

static_assert(offsetof(ContainerMenu, pItemList) == 0x48);
static_assert(offsetof(ContainerMenu, ChangedFormCount) == 0x70);
static_assert(offsetof(BarterMenu, pItemList) == 0x30);
static_assert(offsetof(BarterMenu, ChangedFormCount) == 0x90);
static_assert(offsetof(InventoryMenu, pItemList) == 0x48);
static_assert(offsetof(InventoryMenu, ChangedFormCount) == 0x70);
