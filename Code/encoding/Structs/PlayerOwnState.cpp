#include <Structs/PlayerOwnState.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

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
    FavoriteItems.resize(Serialization::ReadVarInt(aReader) & 0xFFFF);
    for (FavoriteItem& item : FavoriteItems)
    {
        item.Stack.Deserialize(aReader);
        item.Hotkey = static_cast<int8_t>(Serialization::ReadVarInt(aReader) & 0xFF);
    }
    const uint64_t cGlobals = Serialization::ReadVarInt(aReader) & 0xFF;
    for (uint64_t i = 0; i < cGlobals; ++i)
    {
        const uint32_t cType = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        std::string data(Serialization::ReadVarInt(aReader) & 0xFFFFFF, '\0');
        aReader.ReadBytes(std::span(reinterpret_cast<uint8_t*>(data.data()), data.size()));
        Globals[cType] = std::move(data);
    }
    for (float& damage : Damage)
        damage = Serialization::ReadFloat(aReader);
    Effects.resize(Serialization::ReadVarInt(aReader) & 0xFFFF);
    for (Effect& effect : Effects)
    {
        effect.Spell.Deserialize(aReader);
        effect.Index = Serialization::ReadVarInt(aReader) & 0xFF;
        effect.Elapsed = Serialization::ReadFloat(aReader);
    }
    Cooldowns.resize(Serialization::ReadVarInt(aReader) & 0xFFFF);
    for (Cooldown& cooldown : Cooldowns)
    {
        cooldown.Spell.Deserialize(aReader);
        cooldown.Seconds = Serialization::ReadFloat(aReader);
    }
    Trainings = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    FirstPerson = Serialization::ReadBool(aReader);
}
