#include <TiltedOnlinePCH.h>

#include <ExtraData/ExtraContainerChanges.h>

#include <HookSwitch.h>
#include <TESObjectREFR.h>

#include <atomic>

namespace
{
std::atomic<bool> s_rollsWorldContainers{true};

// InventoryChanges::InitLeveledItems (16129): rolls each leveled entry of the owner's base container not rolled yet.
TP_THIS_FUNCTION(TInitLeveledItems, void*, ExtraContainerChanges::Data);
TInitLeveledItems* RealInitLeveledItems = nullptr;

void* TP_MAKE_THISCALL(HookInitLeveledItems, ExtraContainerChanges::Data)
{
    if (!s_rollsWorldContainers.load(std::memory_order_relaxed) && apThis && apThis->parent &&
        apThis->parent->formType == FormType::Reference)
        return nullptr;
    return TiltedPhoques::ThisCall(RealInitLeveledItems, apThis);
}

TiltedPhoques::Initializer s_leveledItemsHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TInitLeveledItems, s_initLeveledItems, 16129);
        RealInitLeveledItems = s_initLeveledItems.Get();
        TP_HOOK(&RealInitLeveledItems, HookInitLeveledItems);
    });
} // namespace

void ExtraContainerChanges::Data::SetRollsWorldContainers(bool aRolls) noexcept
{
    s_rollsWorldContainers.store(aRolls, std::memory_order_relaxed);
}

void ExtraContainerChanges::Data::Save(BGSSaveFormBuffer* apBuffer)
{
    TP_THIS_FUNCTION(TSaveFunc, void*, ExtraContainerChanges::Data, BGSSaveFormBuffer*);

    POINTER_SKYRIMSE(TSaveFunc, s_save, 16142);

    TiltedPhoques::ThisCall(s_save, this, apBuffer);
}

void ExtraContainerChanges::Data::Load(BGSLoadFormBuffer* apBuffer)
{
    TP_THIS_FUNCTION(TLoadFunc, void*, ExtraContainerChanges::Data, BGSLoadFormBuffer*);

    POINTER_SKYRIMSE(TLoadFunc, s_load, 16143);

    TiltedPhoques::ThisCall(s_load, this, apBuffer);
}

bool ExtraContainerChanges::Entry::IsQuestObject() noexcept
{
    TP_THIS_FUNCTION(TIsQuestObject, bool, ExtraContainerChanges::Entry);

    POINTER_SKYRIMSE(TIsQuestObject, s_isQuestObject, 16005);

    return TiltedPhoques::ThisCall(s_isQuestObject, this);
}

TESObjectARMO* ExtraContainerChanges::Data::GetArmor(uint32_t aSlotId) noexcept
{
    TP_THIS_FUNCTION(TGetArmor, TESObjectARMO*, ExtraContainerChanges::Data, uint32_t);

    POINTER_SKYRIMSE(TGetArmor, s_getArmor, 16113);

    return TiltedPhoques::ThisCall(s_getArmor, this, aSlotId);
}
