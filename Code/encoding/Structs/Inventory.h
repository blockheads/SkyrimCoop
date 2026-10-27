#pragma once

#include <functional>
#include <optional>
#include <string>
#include "MagicEquipment.h"
#include <Wire/Buffer.h>

using Wire::Buffer;

struct Inventory
{
    struct EffectItem
    {
        float Magnitude{};
        int32_t Area{};
        int32_t Duration{};
        float RawCost{};
        GameId EffectId{};

        bool operator==(const EffectItem&) const noexcept = default;

        void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
        void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
    };

    struct EnchantmentData
    {
        bool IsWeapon{};
        std::vector<EffectItem> Effects{};

        bool operator==(const EnchantmentData&) const noexcept = default;
    };

    struct Entry
    {
        GameId BaseId{};
        int32_t Count{};

        float ExtraCharge{};

        GameId ExtraEnchantId{};
        uint16_t ExtraEnchantCharge{};
        EnchantmentData EnchantData{};

        float ExtraHealth{};

        GameId ExtraPoisonId{};
        uint32_t ExtraPoisonCount{};

        int32_t ExtraSoulLevel{};

        std::string ExtraName{}; // a player's name for it (ExtraTextDisplayData, custom), no temper suffix

        bool ExtraEnchantRemoveUnequip{};
        bool ExtraWorn{};
        bool ExtraWornLeft{};
        bool IsQuestItem{};

        bool operator==(const Entry& acRhs) const noexcept;
        bool operator!=(const Entry& acRhs) const noexcept;

        void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
        void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

        bool ContainsExtraData() const noexcept { return !IsExtraDataEquals(Entry{}); }

        bool CanBeMerged(const Entry& acRhs) const noexcept { return BaseId == acRhs.BaseId && IsExtraDataEquals(acRhs); }

        bool IsExtraDataEquals(const Entry& acRhs) const noexcept
        {
            // TODO: the whole server side state thing is very flawed
            // since many of these things can and will change, like poison id or charge
            // or the fact that the enchant id can be temp
            return ExtraCharge == acRhs.ExtraCharge && IsSameEnchantment(acRhs) && ExtraEnchantCharge == acRhs.ExtraEnchantCharge && ExtraEnchantRemoveUnequip == acRhs.ExtraEnchantRemoveUnequip && ExtraHealth == acRhs.ExtraHealth && ExtraPoisonId == acRhs.ExtraPoisonId &&
                   ExtraPoisonCount == acRhs.ExtraPoisonCount && ExtraSoulLevel == acRhs.ExtraSoulLevel && ExtraName == acRhs.ExtraName && ExtraWorn == acRhs.ExtraWorn && ExtraWornLeft == acRhs.ExtraWornLeft && IsQuestItem == acRhs.IsQuestItem;
        }

        bool IsWorn() const noexcept { return ExtraWorn || ExtraWornLeft; }

        // A player-made enchantment is each game's own form (a created id): the same effects are the same one.
        bool IsSameEnchantment(const Entry& acRhs) const noexcept
        {
            constexpr uint32_t cCreated = 0xFFFFFFFF;
            if (ExtraEnchantId.ModId == cCreated && acRhs.ExtraEnchantId.ModId == cCreated)
                return EnchantData == acRhs.EnchantData;
            return ExtraEnchantId == acRhs.ExtraEnchantId;
        }
    };

    bool operator==(const Inventory& acRhs) const noexcept;
    bool operator!=(const Inventory& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    std::optional<Entry> GetEntryById(GameId& aItemId) const noexcept;
    int32_t GetEntryCountById(GameId& aItemId) const noexcept;

    void RemoveByFilter(std::function<bool(const Entry&)> aFilter) noexcept;
    void AddOrRemoveEntry(const Entry& acEntry) noexcept;
    void UpdateEquipment(const Inventory& acNewInventory) noexcept;
    bool ContainsQuestItems() const noexcept;

    std::vector<Entry> Entries{};
    MagicEquipment CurrentMagicEquipment{};
};
