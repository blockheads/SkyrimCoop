#include <Bridges/JoinBridge.h>
#include <FormIds.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/IngredientItem.h>
#include <Games/TES.h>
#include <VersionDb.h>

#include <unordered_map>
#include <unordered_set>

namespace
{
constexpr uint32_t kKnown = 1u << 6; // TESForm::kKnown, CommonLibSSE-NG
constexpr uint32_t kIngredientUse = 1u << 31;
} // namespace

PlayerKnowledge JoinBridge::ReadKnowledge() const noexcept
{
    PlayerKnowledge knowledge;
    const auto* pData = ModManager::Get();
    if (!pData)
        return knowledge;
    knowledge.Present = true;
    for (const auto* pIngredient : pData->ingredients)
        if (pIngredient && ((pIngredient->knownEffectFlags & 0xF) || pIngredient->playerUses))
            if (const auto cId = ToSession(pIngredient->formID))
                knowledge.Ingredients.push_back(
                    {*cId, static_cast<uint16_t>(pIngredient->knownEffectFlags & 0xF), pIngredient->playerUses});
    for (const auto* pEnchantment : pData->enchantments)
        if (pEnchantment && (pEnchantment->flags & kKnown))
            if (const auto cId = ToSession(pEnchantment->formID))
                knowledge.Enchantments.push_back(*cId);
    return knowledge;
}

void JoinBridge::ApplyKnowledge(const PlayerKnowledge& acKnowledge) noexcept
{
    auto* pData = ModManager::Get();
    if (!acKnowledge.Present || !pData)
        return;
    std::unordered_map<uint32_t, PlayerKnowledge::Ingredient> ingredients;
    for (const auto& cIngredient : acKnowledge.Ingredients)
        ingredients[ToGame(cIngredient.Id)] = cIngredient;
    for (auto* pIngredient : pData->ingredients)
    {
        if (!pIngredient)
            continue;
        const auto cFound = ingredients.find(pIngredient->formID);
        const uint16_t cEffects = cFound == ingredients.end() ? 0 : cFound->second.Effects & 0xF;
        const uint16_t cUses = cFound == ingredients.end() ? 0 : cFound->second.Uses;
        if ((pIngredient->knownEffectFlags & 0xF) != cEffects || pIngredient->playerUses != cUses)
        {
            pIngredient->knownEffectFlags = (pIngredient->knownEffectFlags & ~0xFu) | cEffects;
            pIngredient->playerUses = cUses;
            pIngredient->MarkChanged(kIngredientUse);
        }
    }
    std::unordered_set<uint32_t> enchantments;
    for (const auto& cId : acKnowledge.Enchantments)
        enchantments.insert(ToGame(cId));
    // TESForm::SetPlayerKnows (14639) updates kKnown and its native change record, without a learned popup.
    POINTER_SKYRIMSE(void(TESForm*, bool), s_setKnown, 14639);
    for (auto* pEnchantment : pData->enchantments)
        if (pEnchantment && ((pEnchantment->flags & kKnown) != 0) != enchantments.contains(pEnchantment->formID))
            s_setKnown(pEnchantment, enchantments.contains(pEnchantment->formID));
}
