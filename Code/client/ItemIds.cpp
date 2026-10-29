#include <ItemIds.h>

#include <FormIds.h>
#include <World.h>

std::optional<GameId> ItemId(uint32_t aFormId) noexcept
{
    if (!IsCreatedForm(aFormId))
        return ToSession(aFormId);
    const auto cRecipe = CreatedItems::RecipeOf(aFormId);
    return cRecipe ? std::optional(World::Get().GetRecipes().Learn(*cRecipe, aFormId)) : std::nullopt;
}

uint32_t ItemForm(const GameId& acId) noexcept
{
    if (!Recipe::IsId(acId))
        return ToGame(acId);
    return World::Get().GetRecipes().FormFor(acId, &CreatedItems::RecipeOf, &CreatedItems::Make);
}
