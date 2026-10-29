#include "Commands.h"
#include "Harness.h"
#include <Forms/EnchantmentItem.h>
#include <Forms/IngredientItem.h>
#include <RTTI.h>

namespace TestHarness
{
namespace
{
nlohmann::json ReadKnowledge(Harness&, const nlohmann::json& acRequest)
{
    TESForm* pForm = TESForm::GetById(acRequest.value("form", 0u));
    if (auto* pIngredient = Cast<IngredientItem>(pForm))
        return {{"ok", true},
                {"name", pIngredient->GetName()},
                {"effects", pIngredient->knownEffectFlags & 0xF},
                {"uses", pIngredient->playerUses}};
    if (auto* pEnchantment = Cast<EnchantmentItem>(pForm))
    {
        // Variants learn their base enchantment. TESForm::kKnown is bit 6 (CommonLibSSE-NG).
        if (pEnchantment->pBaseEnchantment)
            pEnchantment = pEnchantment->pBaseEnchantment;
        return {{"ok", true}, {"form", pEnchantment->formID}, {"known", (pEnchantment->flags & (1u << 6)) != 0}};
    }
    return Error("form is not an ingredient or enchantment");
}
constexpr Command cCommands[] = {{"knowledge", true, &ReadKnowledge}};
} // namespace
std::span<const Command> KnowledgeCommands()
{
    return cCommands;
}
} // namespace TestHarness
