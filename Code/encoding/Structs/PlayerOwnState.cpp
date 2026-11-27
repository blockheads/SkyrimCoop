#include <Structs/PlayerOwnState.h>
#include <Wire/Serialization.h>

#include <algorithm>
#include <cstring>

using Wire::Serialization;

namespace
{
constexpr size_t kMaxGlobal = 1 << 20;
constexpr uint64_t kMaxCount = 4096;
constexpr uint16_t kMaxName = 1023; // ReadBSFixedString's stack buffer is 1024 bytes (36002)

struct Bytes
{
    std::string_view Data;
    size_t At = 0;

    bool Take(size_t aCount) noexcept
    {
        if (aCount > Data.size() - At)
            return false;
        At += aCount;
        return true;
    }
    template <class T> bool Read(T& aValue) noexcept
    {
        if (sizeof(T) > Data.size() - At)
            return false;
        std::memcpy(&aValue, Data.data() + At, sizeof(T));
        At += sizeof(T);
        return true;
    }
    // The engine's variable-size count (BGSLoadFormBuffer 36004): its low 2 bits say 1, 2 or 4 bytes.
    bool Count(uint64_t& aCount) noexcept
    {
        if (At >= Data.size())
            return false;
        const uint8_t cKind = static_cast<uint8_t>(Data[At]) & 3;
        uint32_t raw = 0;
        if (cKind == 0)
        {
            uint8_t value = 0;
            if (!Read(value))
                return false;
            raw = value;
        }
        else if (cKind == 1)
        {
            uint16_t value = 0;
            if (!Read(value))
                return false;
            raw = value;
        }
        else if (cKind != 2 || !Read(raw))
            return false;
        aCount = raw >> 2;
        return aCount <= kMaxCount;
    }
    // A form id as our save hook writes it (SaveLoad.cpp): two varints.
    bool FormId() noexcept
    {
        for (int i = 0; i < 2; ++i)
        {
            uint8_t byte = 0;
            do
                if (!Read(byte))
                    return false;
            while (byte & 0x80);
        }
        return true;
    }
};
} // namespace

bool PlayerOwnState::GlobalReadable(uint32_t aType, std::string_view acData) noexcept
{
    Bytes bytes{acData};
    switch (aType)
    {
    case kMagicFavorites: // spells, then hotkeys: a count and form ids each (52013)
        for (int list = 0; list < 2; ++list)
        {
            uint64_t count = 0;
            if (!bytes.Count(count))
                return false;
            for (uint64_t i = 0; i < count; ++i)
                if (!bytes.FormId())
                    return false;
        }
        return bytes.At == acData.size();
    case kMiscStats: // u32 count; per stat a u16-length name, a category byte, a u32 value (36835)
    {
        uint32_t count = 0;
        if (!bytes.Read(count) || count > kMaxCount)
            return false;
        for (uint32_t i = 0; i < count; ++i)
        {
            uint16_t length = 0;
            if (!bytes.Read(length) || length > kMaxName || !bytes.Take(length + 1u + 4u))
                return false;
        }
        return bytes.At == acData.size();
    }
    case kPlayerControls: // three handler bytes, data+0x28's two, one more (42363, this build's save version)
        return acData.size() == 6;
    default:
        return false;
    }
}

void PlayerOwnState::Age(float aSeconds) noexcept
{
    for (Effect& effect : Effects)
        effect.Elapsed += aSeconds;
    std::erase_if(Effects, [](const Effect& acEffect) { return acEffect.Elapsed >= acEffect.Duration; });
    for (Cooldown& cooldown : Cooldowns)
        cooldown.Seconds -= aSeconds;
    std::erase_if(Cooldowns, [](const Cooldown& acCooldown) { return acCooldown.Seconds <= 0.f; });
}

void PlayerOwnState::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, Present);
    if (!Present)
        return;
    Serialization::WriteVarInt(aWriter, FavoriteItems.size());
    for (const FavoriteItem& cItem : FavoriteItems)
    {
        cItem.Stack.Serialize(aWriter);
        Serialization::WriteVarInt(aWriter, static_cast<uint8_t>(cItem.Hotkey));
    }
    Serialization::WriteVarInt(aWriter, Globals.size());
    for (const auto& [cType, cData] : Globals)
    {
        Serialization::WriteVarInt(aWriter, cType);
        Serialization::WriteVarInt(aWriter, cData.size());
        aWriter.WriteBytes(std::span(reinterpret_cast<const uint8_t*>(cData.data()), cData.size()));
    }
    for (const float cDamage : Damage)
        Serialization::WriteFloat(aWriter, cDamage);
    Serialization::WriteVarInt(aWriter, Effects.size());
    for (const Effect& cEffect : Effects)
    {
        cEffect.Spell.Serialize(aWriter);
        Serialization::WriteVarInt(aWriter, cEffect.Index);
        Serialization::WriteFloat(aWriter, cEffect.Elapsed);
        Serialization::WriteFloat(aWriter, cEffect.Duration);
    }
    Serialization::WriteVarInt(aWriter, Cooldowns.size());
    for (const Cooldown& cCooldown : Cooldowns)
    {
        cCooldown.Spell.Serialize(aWriter);
        Serialization::WriteFloat(aWriter, cCooldown.Seconds);
    }
    Serialization::WriteVarInt(aWriter, Trainings);
    Serialization::WriteBool(aWriter, FirstPerson);
}

void PlayerOwnState::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    *this = {};
    Present = Serialization::ReadBool(aReader);
    if (!Present)
        return;
    FavoriteItems.resize(Serialization::ReadCount(aReader, 0xFFFF));
    for (FavoriteItem& item : FavoriteItems)
    {
        item.Stack.Deserialize(aReader);
        item.Hotkey = static_cast<int8_t>(Serialization::ReadVarInt(aReader) & 0xFF);
    }
    const uint64_t cGlobals = Serialization::ReadCount(aReader, 0xFF);
    for (uint64_t i = 0; i < cGlobals; ++i)
    {
        const uint32_t cType = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        std::string data(Serialization::ReadCount(aReader, kMaxGlobal), '\0');
        if (!aReader.ReadBytes(std::span(reinterpret_cast<uint8_t*>(data.data()), data.size())))
            return void(*this = {});
        // Only the player's globals, and only bytes their loader reads within (it has no bounds check).
        if (std::ranges::find(kGlobals, cType) != kGlobals.end() && GlobalReadable(cType, data))
            Globals[cType] = std::move(data);
    }
    for (float& damage : Damage)
        damage = Serialization::ReadFloat(aReader);
    Effects.resize(Serialization::ReadCount(aReader, 0xFFFF));
    for (Effect& effect : Effects)
    {
        effect.Spell.Deserialize(aReader);
        effect.Index = Serialization::ReadVarInt(aReader) & 0xFF;
        effect.Elapsed = Serialization::ReadFloat(aReader);
        effect.Duration = Serialization::ReadFloat(aReader);
    }
    Cooldowns.resize(Serialization::ReadCount(aReader, 0xFFFF));
    for (Cooldown& cooldown : Cooldowns)
    {
        cooldown.Spell.Deserialize(aReader);
        cooldown.Seconds = Serialization::ReadFloat(aReader);
    }
    Trainings = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    FirstPerson = Serialization::ReadBool(aReader);
}
