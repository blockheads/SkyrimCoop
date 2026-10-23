#include <HookSwitch.h>

#include <EquipManager.h>
#include <Games/References.h>
#include <Games/Overrides.h>

#include <Events/EquipmentChangeEvent.h>

#include <World.h>

#include <Games/Skyrim/Forms/TESAmmo.h>
#include <AI/AIProcess.h>
#include <Games/Skyrim/Misc/MiddleProcess.h>
#include <DefaultObjectManager.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

struct BGSEquipSlot : TESForm
{
};

struct EquipData
{
    ExtraDataList* pExtraDataList; // 0
    int32_t count;                 // 8
    BGSEquipSlot* pSlot;           // 10
    BGSEquipSlot* pSlotToReplace;  // 18
    bool bQueueEquip;
    bool bForceEquip;
    bool bPlaySound;
    bool bApplyNow;
    bool bUnk1;
};
static_assert(sizeof(EquipData) == 0x28);

struct MagicEquipData
{
    BGSEquipSlot* pEquipSlot;
    bool bQueueEquip;
    bool bForceEquip;
};
static_assert(sizeof(MagicEquipData) == 0x10);

struct ShoutEquipData
{
    void* pUnk1;
    bool bUnk2;
};
static_assert(sizeof(MagicEquipData) == 0x10);

using TEquip = void*(EquipManager*, Actor* apActor, TESForm* apItem, EquipData* apData);
using TUnEquip = void*(EquipManager*, Actor* apActor, TESForm* apItem, EquipData* apData);
using TEquipSpell = void*(EquipManager*, Actor* apActor, TESForm* apSpell, MagicEquipData* apData);
using TUnEquipSpell = void*(EquipManager*, Actor* apActor, TESForm* apSpell, MagicEquipData* apData);
using TEquipShout = void*(EquipManager*, Actor* apActor, TESForm* apShout, ShoutEquipData* apData);
using TUnEquipShout = void*(EquipManager*, Actor* apActor, TESForm* apShout, ShoutEquipData* apData);

TUnEquip* RealUnEquip = nullptr;
TEquip* RealEquip = nullptr;
TEquipSpell* RealEquipSpell = nullptr;
TUnEquipSpell* RealUnEquipSpell = nullptr;
TEquipShout* RealEquipShout = nullptr;
TUnEquipShout* RealUnEquipShout = nullptr;

EquipManager* EquipManager::Get() noexcept
{
    POINTER_SKYRIMSE(EquipManager*, s_singleton, 400636);

    return *s_singleton.Get();
}

void* EquipManager::Equip(Actor* apActor, TESForm* apItem, ExtraDataList* apExtraDataList, int aCount, TESForm* apSlot, bool abQueueEquip, bool abForceEquip, bool abPlaySound, bool abApplyNow)
{
    using TEquipInternal = void*(EquipManager*, Actor* apActor, TESForm* apItem, ExtraDataList* apExtraDataList, int aCount, TESForm* apSlot, bool abQueueEquip, bool abForceEquip, bool abPlaySound, bool abApplyNow);
    POINTER_SKYRIMSE(TEquipInternal, s_equipFunc, 38894);

    ScopedEquipOverride equipOverride;

    spdlog::debug("Call Actor[{:X}]::Equip(), item id: {:X}, extra data? {}, count: {}", apActor->formID, apItem->formID, (bool)apExtraDataList, aCount);

    return s_equipFunc(this, apActor, apItem, apExtraDataList, aCount, apSlot, abQueueEquip, abForceEquip, abPlaySound, abApplyNow);
}

void* EquipManager::UnEquip(Actor* apActor, TESForm* apItem, ExtraDataList* apExtraDataList, int aCount, TESForm* apSlot, bool abQueueEquip, bool abForceEquip, bool abPlaySound, bool abApplyNow, TESForm* apSlotToReplace)
{
    using TUnEquipInternal = void*(EquipManager*, Actor* apActor, TESForm* apItem, ExtraDataList* apExtraDataList, int aCount, TESForm* apSlot, bool abQueueEquip, bool abForceEquip, bool abPlaySound, bool abApplyNow, TESForm* apSlotToReplace);
    POINTER_SKYRIMSE(TUnEquipInternal, s_unequipFunc, 38901);

    ScopedEquipOverride equipOverride;

    spdlog::debug("Call Actor[{:X}]::UnEquip(), item id: {:X}, extra data? {}, count: {}", apActor->formID, apItem->formID, (bool)apExtraDataList, aCount);

    return s_unequipFunc(this, apActor, apItem, apExtraDataList, aCount, apSlot, abQueueEquip, abForceEquip, abPlaySound, abApplyNow, apSlotToReplace);
}

void* EquipManager::EquipSpell(Actor* apActor, TESForm* apSpell, uint32_t aSlotId)
{
    using TEquipSpellInternal = void*(EquipManager*, Actor*, TESForm*, uint32_t);
    POINTER_SKYRIMSE(TEquipSpellInternal, s_equipFunc, 38896);

    ScopedEquipOverride equipOverride;

    return s_equipFunc(this, apActor, apSpell, aSlotId);
}

void* EquipManager::UnEquipSpell(Actor* apActor, TESForm* apSpell, uint32_t aSlotId)
{
    using TUnEquipSpellInternal = void*(EquipManager*, Actor*, TESForm*, uint32_t);
    POINTER_SKYRIMSE(TUnEquipSpellInternal, s_unequipFunc, 38903);

    ScopedEquipOverride equipOverride;

    return s_unequipFunc(this, apActor, apSpell, aSlotId);
}

void* EquipManager::EquipShout(Actor* apActor, TESForm* apShout)
{
    using TEquipShoutInternal = void*(EquipManager*, Actor*, TESForm*);
    POINTER_SKYRIMSE(TEquipShoutInternal, s_equipFunc, 38897);

    ScopedEquipOverride equipOverride;

    return s_equipFunc(this, apActor, apShout);
}

void* EquipManager::UnEquipShout(Actor* apActor, TESForm* apShout)
{
    // ActorEquipManager::UnequipShout, the mirror of EquipShout (38897). Not 38903: that is the unequip
    // by slot (left, right, voice) and takes the slot as a fourth argument.
    using TUnEquipShoutInternal = void*(EquipManager*, Actor*, TESForm*);
    POINTER_SKYRIMSE(TUnEquipShoutInternal, s_unequipFunc, 38904);

    ScopedEquipOverride equipOverride;

    return s_unequipFunc(this, apActor, apShout);
}

void EquipManager::UnequipAll(Actor* apActor)
{
    using TUnEquipAll = void(EquipManager*, Actor*);
    POINTER_SKYRIMSE(TUnEquipAll, s_unequipAll, 38899);

    ScopedEquipOverride equipOverride;

    s_unequipAll(this, apActor);
}

void* EquipHook(EquipManager* apThis, Actor* apActor, TESForm* apItem, EquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    if (pExtension->IsCopy())
    {
        // A copy takes only its owner's equipment (ours, under ScopedEquipOverride); the engine's own is refused.
        const bool cOurs = ScopedEquipOverride::IsOverriden();
        spdlog::info("Copy {:X} equip {:X}: {}", apActor->formID, apItem->formID, cOurs ? "ours" : "the engine's, refused");
        if (!cOurs)
            return nullptr;
    }

    // Consumables are "equipped" as well. We don't want this to sync, for several reasons.
    // The right hand item on the server would be overridden by the consumable.
    // Furthermore, the equip action on the other clients would doubly subtract the consumables.
    if (pExtension->IsLocal() && !apItem->IsConsumable() && !apData->bQueueEquip)
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = apActor->formID;
        evt.Count = apData->count;
        evt.ItemId = apItem->formID;
        evt.EquipSlotId = apData->pSlot ? apData->pSlot->formID : 0;
        evt.IsAmmo = apItem->formType == FormType::Ammo;

        World::Get().GetRunner().Trigger(evt);
    }

    ScopedUnequipOverride _;

    return RealEquip(apThis, apActor, apItem, apData);
}

void* UnEquipHook(EquipManager* apThis, Actor* apActor, TESForm* apItem, EquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    if (pExtension->IsCopy())
    {
        // An item leaving the copy is unequipped: refused, the game would keep trying to unequip it.
        const bool cRemoved = ScopedInventoryOverride::IsOverriden() || ScopedItemRemovalOverride::IsOverriden();
        spdlog::info("Copy {:X} unequip {:X}: {}", apActor->formID, apItem->formID,
                     ScopedEquipOverride::IsOverriden() ? "ours" : cRemoved ? "ours (item removed)" : "the engine's, refused");
        if (!ScopedEquipOverride::IsOverriden() && !cRemoved)
            return nullptr;
    }

    if (pExtension->IsLocal() && !ScopedUnequipOverride::IsOverriden() && !apData->bQueueEquip)
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = apActor->formID;
        evt.Count = apData->count;
        evt.ItemId = apItem->formID;
        evt.EquipSlotId = apData->pSlot ? apData->pSlot->formID : 0;
        evt.Unequip = true;
        evt.IsAmmo = apItem->formType == FormType::Ammo;

        World::Get().GetRunner().Trigger(evt);
    }

    spdlog::debug("UnEquipHook, actor: {:X}", apActor->formID);

    return RealUnEquip(apThis, apActor, apItem, apData);
}

void* EquipSpellHook(EquipManager* apThis, Actor* apActor, TESForm* apSpell, MagicEquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    if (pExtension->IsCopy() && !ScopedEquipOverride::IsOverriden())
        return nullptr;

    if (pExtension->IsLocal() && !apData->bQueueEquip)
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = apActor->formID;
        evt.ItemId = apSpell->formID;
        evt.EquipSlotId = apData->pEquipSlot->formID;
        evt.IsSpell = true;

        World::Get().GetRunner().Trigger(evt);
    }

    ScopedUnequipOverride _;

    return RealEquipSpell(apThis, apActor, apSpell, apData);
}

void* UnEquipSpellHook(EquipManager* apThis, Actor* apActor, TESForm* apSpell, MagicEquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    if (pExtension->IsCopy() && !ScopedEquipOverride::IsOverriden())
        return nullptr;

    if (pExtension->IsLocal() && !ScopedUnequipOverride::IsOverriden() && !apData->bQueueEquip)
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = apActor->formID;
        evt.ItemId = apSpell->formID;
        evt.EquipSlotId = apData->pEquipSlot->formID;
        evt.Unequip = true;
        evt.IsSpell = true;

        World::Get().GetRunner().Trigger(evt);
    }

    return RealUnEquipSpell(apThis, apActor, apSpell, apData);
}

void* EquipShoutHook(EquipManager* apThis, Actor* apActor, TESForm* apShout, ShoutEquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    if (pExtension->IsCopy() && !ScopedEquipOverride::IsOverriden())
        return nullptr;

    // TODO: queue check?
    if (pExtension->IsLocal())
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = apActor->formID;
        evt.ItemId = apShout->formID;
        evt.IsShout = true;

        World::Get().GetRunner().Trigger(evt);
    }

    ScopedUnequipOverride _;

    return RealEquipShout(apThis, apActor, apShout, apData);
}

void* UnEquipShoutHook(EquipManager* apThis, Actor* apActor, TESForm* apShout, ShoutEquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    if (pExtension->IsCopy() && !ScopedEquipOverride::IsOverriden())
        return nullptr;

    // TODO: queue check?
    if (pExtension->IsLocal() && !ScopedUnequipOverride::IsOverriden())
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = apActor->formID;
        evt.ItemId = apShout->formID;
        evt.Unequip = true;
        evt.IsShout = true;

        World::Get().GetRunner().Trigger(evt);
    }

    return RealUnEquipShout(apThis, apActor, apShout, apData);
}

static Hooks::Initializer s_equipmentHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TEquip, s_equipFunc, 38929);
        POINTER_SKYRIMSE(TUnEquip, s_unequipFunc, 38934);

        RealEquip = s_equipFunc.Get();
        RealUnEquip = s_unequipFunc.Get();

        HOOK(&RealEquip, EquipHook);
        HOOK(&RealUnEquip, UnEquipHook);
        HOOK_ID(38928, &RealEquipSpell, EquipSpellHook);
        HOOK_ID(38933, &RealUnEquipSpell, UnEquipSpellHook);
        HOOK_ID(38930, &RealEquipShout, EquipShoutHook);
        HOOK_ID(38935, &RealUnEquipShout, UnEquipShoutHook);
    });
