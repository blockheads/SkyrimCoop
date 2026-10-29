#pragma once

#include <Structs/Recipe.h>

#include <cstdint>
#include <optional>

// An item's id in the session (FormIds), a player-made potion or poison's by its recipe, learned into this game's
// book (RecipeBook, docs/design/crafted-potions.md). None for another created form.
[[nodiscard]] std::optional<GameId> ItemId(uint32_t aFormId) noexcept;
// This game's form for an item's id, a recipe's made here; 0 for none.
[[nodiscard]] uint32_t ItemForm(const GameId& acId) noexcept;

// The engine's part (AlchemyItem; the fake engine's in GameTests).
namespace CreatedItems
{
// A created form's recipe; none for one that is no player-made potion or poison.
[[nodiscard]] std::optional<Recipe> RecipeOf(uint32_t aFormId) noexcept;
// A new form for the recipe (the engine's own with the same effects); 0 for none.
[[nodiscard]] uint32_t Make(const Recipe& acRecipe) noexcept;
} // namespace CreatedItems
