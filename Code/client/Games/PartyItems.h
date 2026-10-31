#pragma once

#include <Structs/Inventory.h>

#include <cstdint>
#include <string>
#include <string_view>

// Party items (docs/design/party-items.md): keys, quest items and the story's rewards, every player carrying a copy
// named "[P] <name>". Pure rules, checked by ClientTests.
namespace PartyItems
{
inline constexpr std::string_view cTag = "[P] ";
inline constexpr uint8_t cKeyFormType = 45; // KEYM (FormType::KeyMaster)
inline constexpr uint32_t cGold = 0xF;       // Gold001: the party's purse holds it (party-purse.md), never a party item

// A stack the party owns: tagged, or a quest item (its alias's name stays; the quest marker shows it).
[[nodiscard]] inline bool IsParty(const Inventory::Entry& acItem) noexcept
{
    return acItem.IsQuestItem || acItem.ExtraName.starts_with(cTag);
}

// An item arriving in a player's inventory is the party's: a key, a quest item, or the story's (a quest's script).
[[nodiscard]] inline bool ArrivesAsParty(uint32_t aFormId, uint8_t aFormType, bool aQuestItem, bool aStoryGiven) noexcept
{
    return aFormId != cGold && (aFormType == cKeyFormType || aQuestItem || aStoryGiven);
}

// A carried stack is that party item: the same base, name ("[P] <name>") and improvements (tempering, enchantment), not
// what each copy's own use changes (charge, poison); for a party quest item, also the engine's quest stack (its
// alias's name), which only its holder carries.
[[nodiscard]] inline bool SameStack(const Inventory::Entry& acParty, const Inventory::Entry& acStack) noexcept
{
    if (acParty.BaseId != acStack.BaseId)
        return false;
    if (acStack.IsQuestItem)
        return acParty.IsQuestItem;
    return acStack.ExtraName == acParty.ExtraName && acStack.ExtraHealth == acParty.ExtraHealth &&
           acStack.IsSameEnchantment(acParty) && acStack.ExtraEnchantCharge == acParty.ExtraEnchantCharge;
}

// Its name as a party copy: "[P] " in front of the custom name, else of the form's name.
[[nodiscard]] inline std::string Tagged(std::string_view aCustomName, std::string_view aFormName)
{
    const std::string_view cName = aCustomName.empty() ? aFormName : aCustomName;
    return cName.starts_with(cTag) ? std::string(cName) : std::string(cTag) + std::string(cName);
}
} // namespace PartyItems
