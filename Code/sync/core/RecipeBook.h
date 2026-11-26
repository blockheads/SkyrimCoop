#pragma once

#include <Structs/Recipe.h>

#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

// The player-made potions' recipes this game knows, in the order it learned them: its players' own and those the
// session told it, with this game's form for each (docs/design/crafted-potions.md). Used on any thread.
struct RecipeBook
{
    using RecipeOf = std::function<std::optional<Recipe>(uint32_t aFormId)>;
    using Make = std::function<uint32_t(const Recipe& acRecipe)>;

    // Its id; kept if new. With aFormId: this game's form for it.
    GameId Learn(const Recipe& acRecipe, uint32_t aFormId = 0) noexcept;
    [[nodiscard]] std::optional<Recipe> Find(const GameId& acId) const noexcept;
    // This game's form for a recipe id: the one it had while still that potion (acRecipeOf), else acMake's; 0 for an
    // id not learned or not made.
    [[nodiscard]] uint32_t FormFor(const GameId& acId, const RecipeOf& acRecipeOf, const Make& acMake) noexcept;
    // Those learned since a game was told aSent of them, which it is now (aSent counts them).
    [[nodiscard]] std::vector<Recipe> Unsent(size_t& aSent) const noexcept;
    // The recipes an inventory's items and coatings name (a character record's, PlayerRecord::Recipes).
    [[nodiscard]] std::vector<Recipe> Of(const Inventory& acInventory) const noexcept;

private:
    mutable std::mutex m_lock;
    std::vector<Recipe> m_recipes;
    std::unordered_map<GameId, size_t> m_index; // its place in m_recipes
    std::unordered_map<GameId, uint32_t> m_forms;
};
