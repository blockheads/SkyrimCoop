#include "EnchantmentItem.h"

#include <Systems/ModSystem.h>
#include <Magic/EffectItem.h>
#include <World.h>
#include <Games/BGSCreatedObjectManager.h>

EnchantmentItem* EnchantmentItem::Create(const Inventory::EnchantmentData& aData) noexcept
{
    using TAddWeaponEnchantment = EnchantmentItem*(void*, GameArray<EffectItem>*);
    POINTER_SKYRIMSE(TAddWeaponEnchantment, addWeaponEnchantment, 36165);
    using TAddArmorEnchantment = EnchantmentItem*(void*, GameArray<EffectItem>*);
    POINTER_SKYRIMSE(TAddArmorEnchantment, addArmorEnchantment, 36166);

    ModSystem& modSystem = World::Get().GetModSystem();

    GameArray<EffectItem> effects{};
    effects.Resize(aData.Effects.size());
    for (int i = 0; i < aData.Effects.size(); i++)
    {
        Inventory::EffectItem effect = aData.Effects[i];

        EffectItem effectItem{};
        effectItem.data.fMagnitude = effect.Magnitude;
        effectItem.data.iArea = effect.Area;
        effectItem.data.iDuration = effect.Duration;
        effectItem.fRawCost = effect.RawCost;
        effectItem.pEffectSetting = Cast<EffectSetting>(TESForm::GetById(modSystem.GetGameId(effect.EffectId)));
        if (!effectItem.pEffectSetting)
            spdlog::error("Effect setting not found: {:X}:{:X}", effect.EffectId.ModId, effect.EffectId.BaseId);

        // TODO: TESCondition?

        effects[i] = effectItem;
    }

    BGSCreatedObjectManager* objManager = BGSCreatedObjectManager::Get();

    EnchantmentItem* pItem = nullptr;
    if (aData.IsWeapon)
    {
        pItem = addWeaponEnchantment(objManager, &effects);
    }
    else
    {
        pItem = addArmorEnchantment(objManager, &effects);
    }

    return pItem;
}
