#pragma once

#include "ExtraData/ExtraData.h"

#include <ExtraData/ExtraSoul.h>

struct AlchemyItem;
struct EnchantmentItem;

struct ExtraDataList
{
    static ExtraDataList* New() noexcept;

    bool Contains(ExtraDataType aType) const;
    void Set(ExtraDataType aType, bool aSet);

    bool Add(ExtraDataType aType, BSExtraData* apNewData);
    bool Remove(ExtraDataType aType, BSExtraData* apNewData);
    // Removes and frees the entry of that type, as the engine does (12354).
    void RemoveByType(ExtraDataType aType) noexcept;

    uint32_t GetCount() const;

    void SetType(ExtraDataType aType, bool aClear);
    BSExtraData* GetByType(ExtraDataType type) const;

    void SetSoulData(SOUL_LEVEL aSoulLevel) noexcept;
    void SetChargeData(float aCharge) noexcept;
    void SetWorn(bool aWornLeft) noexcept;
    void SetPoison(AlchemyItem* apItem, uint32_t aCount) noexcept;
    void SetHealth(float aHealth) noexcept;
    void SetEnchantmentData(EnchantmentItem* apItem, uint16_t aCharge, bool aRemoveOnUnequip) noexcept;
    // How many the reference or stack stands for (ExtraCount; 1 removes it), as ExtraDataList::SetCount (11617).
    void SetCount(uint16_t aCount) noexcept;
    // A player's name for the item (ExtraTextDisplayData, custom), as CommonLib's SetOverrideName; none over a quest's
    // or a message's name.
    // False when a quest's or message's name keeps it.
    bool SetCustomName(const char* acpName, bool aOverQuest = false) noexcept;
    // Who owns the item or reference (ExtraOwnership: an NPC or a faction), as ExtraDataList::SetOwnerForm (11609).
    void SetOwner(TESForm* apOwner) noexcept;
    // A favorite's number key (ExtraHotkey: -1 favorited, unbound; 0-7), added if absent (ExtraDataList::SetHotkey 11664).
    void SetHotkey(int8_t aHotkey) noexcept;
    // ExtraDataList::SetInteractionData (12074): adds only to an empty slot; a null interaction empties it.
    void SetInteraction(struct RefrInteraction** appInteraction) noexcept;

    [[nodiscard]] bool HasQuestObjectAlias() noexcept;

    virtual ~ExtraDataList();
    BSExtraData* data = nullptr;

    struct Bitfield
    {
        uint8_t data[0x18];
    };

    Bitfield* bitfield{};
    // A reader/writer lock in 1.6.1170 (it was handled as a spin lock, which strands the engine).
    mutable BSReadWriteLock lock{};
};

static_assert(offsetof(ExtraDataList, lock) == 0x18); // BSReadWriteLock at +0x18 (11590, 11591)
