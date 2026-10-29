#pragma once

#include "MagicItem.h"

#include <Structs/Recipe.h>

#include <optional>

// A potion, poison or food (CommonLibSSE-NG AlchemyItem, ALCH).
struct AlchemyItem : MagicItem
{
    enum Flags : uint32_t
    {
        kFood = 1 << 1,
        kPoison = 1 << 17,
    };

    // ENIT
    struct Data
    {
        int32_t costOverride;    // 00
        uint32_t flags;          // 04
        void* pAddiction;        // 08
        float addictionChance;   // 10
        uint32_t pad14;          // 14
        void* pConsumptionSound; // 18
    };
    static_assert(sizeof(Data) == 0x20);

    // A player-made potion or poison's recipe; none for a plugin's form, food, or effects another game cannot make
    // (a created effect, a condition).
    [[nodiscard]] static std::optional<Recipe> RecipeOf(const TESForm* apForm) noexcept;
    // This game's potion or poison for it, as the alchemy menu makes one (AddPotion 36167, AddPoison 36168): the
    // engine's own when it has one with the same effects. Its reference is kept, as any inventory may hold the form.
    [[nodiscard]] static AlchemyItem* Make(const Recipe& acRecipe) noexcept;

    [[nodiscard]] bool IsPoison() const noexcept { return (data.flags & kPoison) != 0; }
    [[nodiscard]] bool IsFood() const noexcept { return (data.flags & kFood) != 0 && !IsPoison(); }

    uint8_t pad90[0x138 - sizeof(MagicItem)];
    Data data; // 138
    uint8_t pad158[0x10];
};

static_assert(offsetof(AlchemyItem, data) == 0x138);
static_assert(sizeof(AlchemyItem) == 0x168);
