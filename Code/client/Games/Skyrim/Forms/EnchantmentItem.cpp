#include "EnchantmentItem.h"

#include <Games/BGSCreatedObjectManager.h>
#include <VersionDb.h>

EnchantmentItem* EnchantmentItem::Create(const Inventory::EnchantmentData& aData) noexcept
{
    // BGSCreatedObjectManager::AddWeaponEnchantment / AddArmorEnchantment.
    using TAddEnchantment = EnchantmentItem*(BGSCreatedObjectManager*, GameArray<EffectItem>*);
    POINTER_SKYRIMSE(TAddEnchantment, s_addWeaponEnchantment, 36165);
    POINTER_SKYRIMSE(TAddEnchantment, s_addArmorEnchantment, 36166);

    NewEffects effects;
    if (!effects.Set(aData.Effects))
        return nullptr;
    return (aData.IsWeapon ? s_addWeaponEnchantment : s_addArmorEnchantment)(BGSCreatedObjectManager::Get(), &effects.Array);
}
