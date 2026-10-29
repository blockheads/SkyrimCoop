#pragma once
#include <Forms/MagicItem.h>
#include <cstddef>

// CommonLibSSE-NG IngredientItem: item components to 0x130, then ENIT and per-save discovery data.
struct IngredientItem : MagicItem
{
    std::byte itemComponents[0xA0];
    int32_t costOverride;
    uint32_t ingredientFlags;
    uint16_t knownEffectFlags;
    uint16_t playerUses;
    uint32_t pad13C;
};
static_assert(offsetof(IngredientItem, knownEffectFlags) == 0x138);
static_assert(sizeof(IngredientItem) == 0x140);
