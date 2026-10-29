#include <RecipeBook.h>

#include <spdlog/spdlog.h>

#include <algorithm>

GameId RecipeBook::Learn(const Recipe& acRecipe, uint32_t aFormId) noexcept
{
    const GameId cId = acRecipe.Id();
    std::scoped_lock _{m_lock};
    if (aFormId)
        m_forms[cId] = aFormId;
    if (const auto it = m_index.find(cId); it != m_index.end())
    {
        if (!(m_recipes[it->second] == acRecipe))
            spdlog::warn("RecipeBook: two recipes share id {:X}; the first is kept", cId.BaseId);
        return cId;
    }
    m_index.emplace(cId, m_recipes.size());
    m_recipes.push_back(acRecipe);
    return cId;
}

std::optional<Recipe> RecipeBook::Find(const GameId& acId) const noexcept
{
    std::scoped_lock _{m_lock};
    const auto it = m_index.find(acId);
    return it != m_index.end() ? std::optional(m_recipes[it->second]) : std::nullopt;
}

uint32_t RecipeBook::FormFor(const GameId& acId, const RecipeOf& acRecipeOf, const Make& acMake) noexcept
{
    const auto cRecipe = Find(acId);
    if (!cRecipe)
    {
        spdlog::warn("RecipeBook: no recipe {:X} here", acId.BaseId);
        return 0;
    }
    uint32_t made = 0;
    {
        std::scoped_lock _{m_lock};
        if (const auto it = m_forms.find(acId); it != m_forms.end())
            made = it->second;
    }
    // A created form goes once nothing holds it: the one made before only while it is still that potion.
    if (made && acRecipeOf(made) == cRecipe)
        return made;
    made = acMake(*cRecipe);
    if (!made)
        spdlog::warn("RecipeBook: recipe {:X} not made here", acId.BaseId);
    else
        Learn(*cRecipe, made);
    return made;
}

std::vector<Recipe> RecipeBook::Unsent(size_t& aSent) const noexcept
{
    std::scoped_lock _{m_lock};
    if (aSent >= m_recipes.size())
        return {};
    std::vector unsent(m_recipes.begin() + static_cast<ptrdiff_t>(aSent), m_recipes.end());
    aSent = m_recipes.size();
    return unsent;
}

std::vector<Recipe> RecipeBook::Of(const Inventory& acInventory) const noexcept
{
    std::vector<Recipe> recipes;
    for (const auto& cEntry : acInventory.Entries)
        for (const GameId& cId : {cEntry.BaseId, cEntry.ExtraPoisonId})
            if (Recipe::IsId(cId))
                if (auto recipe = Find(cId); recipe && std::ranges::find(recipes, *recipe) == recipes.end())
                    recipes.push_back(std::move(*recipe));
    return recipes;
}
