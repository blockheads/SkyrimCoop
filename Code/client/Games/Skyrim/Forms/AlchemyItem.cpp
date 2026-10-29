#include "AlchemyItem.h"

#include <FormIds.h>
#include <Games/BGSCreatedObjectManager.h>
#include <ItemIds.h>
#include <RTTI.h>
#include <VersionDb.h>

std::optional<Recipe> AlchemyItem::RecipeOf(const TESForm* apForm) noexcept
{
    const auto* pItem = apForm && IsCreatedForm(apForm->formID) ? Cast<AlchemyItem>(const_cast<TESForm*>(apForm)) : nullptr;
    if (!pItem || pItem->IsFood())
        return std::nullopt;
    // An effect's conditions do not travel: such a potion is its game's own.
    for (const EffectItem* pEffect : pItem->listOfEffects)
        if (pEffect && pEffect->Condition.pHead)
            return std::nullopt;
    auto effects = pItem->SessionEffects();
    if (!effects || effects->empty())
        return std::nullopt;
    return Recipe{pItem->IsPoison(), std::move(*effects)};
}

AlchemyItem* AlchemyItem::Make(const Recipe& acRecipe) noexcept
{
    // BGSCreatedObjectManager::AddPotion / AddPoison: the manager, the made form (a created-object smart pointer), the effects.
    using TAdd = void(BGSCreatedObjectManager*, AlchemyItem**, GameArray<EffectItem>*);
    POINTER_SKYRIMSE(TAdd, s_addPotion, 36167);
    POINTER_SKYRIMSE(TAdd, s_addPoison, 36168);

    auto* pManager = BGSCreatedObjectManager::Get();
    NewEffects effects;
    if (!pManager || acRecipe.Effects.empty() || !effects.Set(acRecipe.Effects))
        return nullptr;
    AlchemyItem* pMade = nullptr;
    (acRecipe.IsPoison ? s_addPoison : s_addPotion)(pManager, &pMade, &effects.Array);
    return pMade;
}

std::optional<Recipe> CreatedItems::RecipeOf(uint32_t aFormId) noexcept
{
    return AlchemyItem::RecipeOf(TESForm::GetById(aFormId));
}

uint32_t CreatedItems::Make(const Recipe& acRecipe) noexcept
{
    const AlchemyItem* pMade = AlchemyItem::Make(acRecipe);
    return pMade ? pMade->formID : 0;
}
