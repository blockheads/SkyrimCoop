#include "ExtraDataList.h"
#include "ExtraTextDisplayData.h"

#include <Games/Memory.h>
#include <Games/Overrides.h>

#include <cstring>
#include <VersionDb.h>

ExtraDataList* ExtraDataList::New() noexcept
{
    ExtraDataList* pExtraDataList = Memory::Allocate<ExtraDataList>();
    pExtraDataList->data = nullptr;
    pExtraDataList->lock.m_lockCount = 0;
    pExtraDataList->lock.m_writerThread = 0;
    pExtraDataList->bitfield = Memory::Allocate<ExtraDataList::Bitfield>();
    memset(pExtraDataList->bitfield, 0, 0x18);
    return pExtraDataList;
}

bool ExtraDataList::Contains(ExtraDataType aType) const
{
    if (bitfield)
    {
        const auto value = static_cast<uint32_t>(aType);
        const auto index = value >> 3;

        const auto element = bitfield->data[index];

        return (element >> (value % 8)) & 1;
    }

    return false;
}

BSExtraData* ExtraDataList::GetByType(ExtraDataType aType) const
{
    if (!ScopedExtraDataOverride::IsOverriden())
        lock.LockForRead();

    if (!Contains(aType))
    {
        if (!ScopedExtraDataOverride::IsOverriden())
            lock.UnlockForRead();
        return nullptr;
    }

    auto pEntry = data;
    while (pEntry != nullptr && pEntry->GetType() != aType)
    {
        pEntry = pEntry->next;
    }

    if (!ScopedExtraDataOverride::IsOverriden())
        lock.UnlockForRead();

    return pEntry;
}

bool ExtraDataList::Add(ExtraDataType aType, BSExtraData* apNewData)
{
    if (Contains(aType))
        return false;

    BSWriteLockGuard _(lock);

    BSExtraData* pNext = data;
    data = apNewData;
    apNewData->next = pNext;
    SetType(aType, false);

    return true;
}

void ExtraDataList::RemoveByType(ExtraDataType aType) noexcept
{
    using TRemoveByType = void(ExtraDataList*, ExtraDataType);
    POINTER_SKYRIMSE(TRemoveByType, s_removeByType, 12354);
    s_removeByType(this, aType);
}

uint32_t ExtraDataList::GetCount() const
{
    uint32_t count = 0;

    BSExtraData* pNext = data;
    while (pNext)
    {
        count++;
        pNext = pNext->next;
    }

    return count;
}

void ExtraDataList::SetType(ExtraDataType aType, bool aClear)
{
    uint32_t index = static_cast<uint8_t>(aType) >> 3;
    uint8_t bitmask = 1 << (static_cast<uint8_t>(aType) % 8);
    uint8_t& flag = bitfield->data[index];
    if (aClear)
        flag &= ~bitmask;
    else
        flag |= bitmask;
}

void ExtraDataList::SetSoulData(SOUL_LEVEL aSoulLevel) noexcept
{
    using TSetSoulData = void(ExtraDataList*, SOUL_LEVEL aSoulLevel);
    POINTER_SKYRIMSE(TSetSoulData, setSoulData, 11620);
    setSoulData(this, aSoulLevel);
}

void ExtraDataList::SetChargeData(float aCharge) noexcept
{
    using TSetChargeData = void(ExtraDataList*, float aCharge);
    POINTER_SKYRIMSE(TSetChargeData, setChargeData, 11619);
    setChargeData(this, aCharge);
}

void ExtraDataList::SetWorn(bool aWornLeft) noexcept
{
    // TODO: what's this bool? seems to be true always except for one instance
    using TSetWornData = void(ExtraDataList*, bool aUnk1, bool aWornLeft);
    POINTER_SKYRIMSE(TSetWornData, setWornData, 11612);
    setWornData(this, true, aWornLeft);
}

void ExtraDataList::SetPoison(AlchemyItem* apItem, uint32_t aCount) noexcept
{
    using TSetPoison = void(ExtraDataList*, AlchemyItem* apItem, uint32_t aCount);
    POINTER_SKYRIMSE(TSetPoison, setPoison, 11822);
    setPoison(this, apItem, aCount);
}

void ExtraDataList::SetHealth(float aHealth) noexcept
{
    using TSetHealth = void(ExtraDataList*, float aHealth);
    POINTER_SKYRIMSE(TSetHealth, setHealth, 11616);
    setHealth(this, aHealth);
}

void ExtraDataList::SetEnchantmentData(EnchantmentItem* apItem, uint16_t aCharge, bool aRemoveOnUnequip) noexcept
{
    using TSetEnchantmentData = void(ExtraDataList*, EnchantmentItem* apItem, uint16_t aCharge, bool aRemoveOnUnequip);
    POINTER_SKYRIMSE(TSetEnchantmentData, setEnchantmentData, 12060);
    setEnchantmentData(this, apItem, aCharge, aRemoveOnUnequip);
}

void ExtraDataList::SetCount(uint16_t aCount) noexcept
{
    using TSetCount = void(ExtraDataList*, uint16_t aCount);
    POINTER_SKYRIMSE(TSetCount, s_setCount, 11617);
    s_setCount(this, aCount);
}

void ExtraDataList::SetCustomName(const char* acpName) noexcept
{
    auto* pText = static_cast<ExtraTextDisplayData*>(GetByType(ExtraDataType::TextDisplayData));
    if (!pText)
    {
        // As SKSE's ExtraTextDisplayData::Create: its vtable (186855), no owner instance, temper factor 1.
        POINTER_SKYRIMSE(uint8_t, s_vtable, 186855);
        pText = static_cast<ExtraTextDisplayData*>(Memory::Allocate(sizeof(ExtraTextDisplayData)));
        std::memset(static_cast<void*>(pText), 0, sizeof(ExtraTextDisplayData));
        *reinterpret_cast<void**>(pText) = s_vtable.Get();
        pText->iOwnerInstance = -1;
        pText->fTemperFactor = 1.f;
        Add(ExtraDataType::TextDisplayData, pText);
    }
    if (pText->pDisplayNameText || pText->pOwnerQuest)
        return;
    pText->DisplayName.Set(acpName);
    pText->usCustomNameLength = static_cast<uint16_t>(std::strlen(acpName));
    pText->iOwnerInstance = ExtraTextDisplayData::kCustomName;
    pText->fTemperFactor = 1.f;
}

bool ExtraDataList::HasQuestObjectAlias() noexcept
{
    using THasQuestObjectAlias = bool(ExtraDataList*);
    POINTER_SKYRIMSE(THasQuestObjectAlias, s_hasQuestObjectAlias, 12052);
    return s_hasQuestObjectAlias(this);
}
