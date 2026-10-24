#include <Structs/PlayerRecord.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

namespace
{
void WriteIds(Wire::Buffer::Writer& aWriter, const std::vector<GameId>& acIds) noexcept
{
    Serialization::WriteVarInt(aWriter, acIds.size());
    for (const GameId& cId : acIds)
        cId.Serialize(aWriter);
}

// A blob past WriteString's 64 KB: its full size first.
void WriteBlob(Wire::Buffer::Writer& aWriter, const std::string& acData) noexcept
{
    Serialization::WriteVarInt(aWriter, acData.size());
    aWriter.WriteBytes(std::span(reinterpret_cast<const uint8_t*>(acData.data()), acData.size()));
}

std::string ReadBlob(Wire::Buffer::Reader& aReader) noexcept
{
    std::string data(Serialization::ReadVarInt(aReader) & 0xFFFFFF, '\0');
    aReader.ReadBytes(std::span(reinterpret_cast<uint8_t*>(data.data()), data.size()));
    return data;
}

std::vector<GameId> ReadIds(Wire::Buffer::Reader& aReader) noexcept
{
    std::vector<GameId> ids(Serialization::ReadVarInt(aReader) & 0xFFFF);
    for (GameId& id : ids)
        id.Deserialize(aReader);
    return ids;
}
} // namespace

bool PlayerRecord::operator==(const PlayerRecord& acRhs) const noexcept
{
    return Key == acRhs.Key && Name == acRhs.Name && Level == acRhs.Level && Base == acRhs.Base && BaseFlags == acRhs.BaseFlags &&
           FaceTints == acRhs.FaceTints && Weight == acRhs.Weight && LevelExperience == acRhs.LevelExperience && LevelThreshold == acRhs.LevelThreshold &&
           Skills == acRhs.Skills && PerkPoints == acRhs.PerkPoints && Perks == acRhs.Perks && Spells == acRhs.Spells && Values == acRhs.Values &&
           Items == acRhs.Items && Extras == acRhs.Extras && Cell == acRhs.Cell && WorldSpace == acRhs.WorldSpace && Position == acRhs.Position;
}

void PlayerRecord::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Key);
    Serialization::WriteString(aWriter, Name);
    Serialization::WriteVarInt(aWriter, Level);

    Serialization::WriteString(aWriter, Base);
    Serialization::WriteVarInt(aWriter, BaseFlags);
    FaceTints.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Weight);

    Serialization::WriteFloat(aWriter, LevelExperience);
    Serialization::WriteFloat(aWriter, LevelThreshold);
    for (const Skill& cSkill : Skills)
    {
        Serialization::WriteFloat(aWriter, cSkill.Level);
        Serialization::WriteFloat(aWriter, cSkill.Experience);
        Serialization::WriteFloat(aWriter, cSkill.Threshold);
        Serialization::WriteVarInt(aWriter, cSkill.Legendary);
    }
    Serialization::WriteVarInt(aWriter, PerkPoints);
    Serialization::WriteVarInt(aWriter, Perks.size());
    for (const Perk& cPerk : Perks)
    {
        cPerk.Id.Serialize(aWriter);
        Serialization::WriteVarInt(aWriter, cPerk.Rank);
    }
    WriteIds(aWriter, Spells);
    Serialization::WriteVarInt(aWriter, Values.size());
    for (const Value& cValue : Values)
    {
        Serialization::WriteVarInt(aWriter, cValue.Index);
        Serialization::WriteFloat(aWriter, cValue.Base);
    }

    Items.Serialize(aWriter);

    Serialization::WriteVarInt(aWriter, Extras.size());
    for (const auto& [cName, cData] : Extras)
    {
        Serialization::WriteString(aWriter, cName);
        WriteBlob(aWriter, cData);
    }

    Cell.Serialize(aWriter);
    WorldSpace.Serialize(aWriter);
    for (const float cValue : Position)
        Serialization::WriteFloat(aWriter, cValue);
}

void PlayerRecord::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    Key = Serialization::ReadString(aReader);
    Name = Serialization::ReadString(aReader);
    Level = Serialization::ReadVarInt(aReader) & 0xFFFF;

    Base = Serialization::ReadString(aReader);
    BaseFlags = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    FaceTints = {};
    FaceTints.Deserialize(aReader);
    Weight = Serialization::ReadFloat(aReader);

    LevelExperience = Serialization::ReadFloat(aReader);
    LevelThreshold = Serialization::ReadFloat(aReader);
    for (Skill& skill : Skills)
    {
        skill.Level = Serialization::ReadFloat(aReader);
        skill.Experience = Serialization::ReadFloat(aReader);
        skill.Threshold = Serialization::ReadFloat(aReader);
        skill.Legendary = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    }
    PerkPoints = Serialization::ReadVarInt(aReader) & 0xFF;
    Perks.resize(Serialization::ReadVarInt(aReader) & 0xFFFF);
    for (Perk& perk : Perks)
    {
        perk.Id.Deserialize(aReader);
        perk.Rank = Serialization::ReadVarInt(aReader) & 0xFF;
    }
    Spells = ReadIds(aReader);
    Values.resize(Serialization::ReadVarInt(aReader) & 0xFF);
    for (Value& value : Values)
    {
        value.Index = Serialization::ReadVarInt(aReader) & 0xFF;
        value.Base = Serialization::ReadFloat(aReader);
    }

    Items = {};
    Items.Deserialize(aReader);

    Extras.clear();
    const uint64_t cExtras = Serialization::ReadVarInt(aReader) & 0xFF;
    for (uint64_t i = 0; i < cExtras; ++i)
    {
        std::string name = Serialization::ReadString(aReader);
        Extras[std::move(name)] = ReadBlob(aReader);
    }

    Cell.Deserialize(aReader);
    WorldSpace.Deserialize(aReader);
    for (float& value : Position)
        value = Serialization::ReadFloat(aReader);
}
