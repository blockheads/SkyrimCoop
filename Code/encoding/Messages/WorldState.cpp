#include <Messages/WorldState.h>

#include <TiltedCore/Serialization.hpp>

namespace
{
// Skyrim has about 1100 globals and 1500 quests; more than these is a corrupt count.
constexpr uint64_t kMaxGlobals = 0x10000;
constexpr uint64_t kMaxQuests = 0x10000;
constexpr uint64_t kMaxPerQuest = 0x1000;
constexpr uint64_t kMaxScriptResults = 0x1000;

void Write(TiltedPhoques::Buffer::Writer& aWriter, const QuestState& acQuest) noexcept
{
    acQuest.Id.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, acQuest.Flags);
    Serialization::WriteVarInt(aWriter, acQuest.CurrentStage);
    Serialization::WriteVarInt(aWriter, acQuest.DoneStages.size());
    for (const uint16_t cStage : acQuest.DoneStages)
        Serialization::WriteVarInt(aWriter, cStage);
    Serialization::WriteVarInt(aWriter, acQuest.Objectives.size());
    for (const auto& cObjective : acQuest.Objectives)
    {
        Serialization::WriteVarInt(aWriter, cObjective.Index);
        Serialization::WriteVarInt(aWriter, cObjective.State);
    }
    Serialization::WriteVarInt(aWriter, acQuest.Aliases.size());
    for (const auto& cFill : acQuest.Aliases)
    {
        Serialization::WriteVarInt(aWriter, cFill.AliasId);
        cFill.Ref.Serialize(aWriter);
        Serialization::WriteBool(aWriter, cFill.Created);
    }
}

bool Read(TiltedPhoques::Buffer::Reader& aReader, QuestState& aQuest) noexcept
{
    aQuest.Id.Deserialize(aReader);
    aQuest.Flags = static_cast<uint16_t>(Serialization::ReadVarInt(aReader));
    aQuest.CurrentStage = static_cast<uint16_t>(Serialization::ReadVarInt(aReader));
    uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > kMaxPerQuest)
        return false;
    aQuest.DoneStages.resize(count);
    for (auto& stage : aQuest.DoneStages)
        stage = static_cast<uint16_t>(Serialization::ReadVarInt(aReader));
    count = Serialization::ReadVarInt(aReader);
    if (count > kMaxPerQuest)
        return false;
    aQuest.Objectives.resize(count);
    for (auto& objective : aQuest.Objectives)
    {
        objective.Index = static_cast<uint16_t>(Serialization::ReadVarInt(aReader));
        objective.State = static_cast<uint8_t>(Serialization::ReadVarInt(aReader));
    }
    count = Serialization::ReadVarInt(aReader);
    if (count > kMaxPerQuest)
        return false;
    aQuest.Aliases.resize(count);
    for (auto& fill : aQuest.Aliases)
    {
        fill.AliasId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
        fill.Ref.Deserialize(aReader);
        fill.Created = Serialization::ReadBool(aReader);
    }
    return true;
}
} // namespace

void NotifyGlobals::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Values.size());
    for (const auto& cValue : Values)
    {
        cValue.Id.Serialize(aWriter);
        Serialization::WriteFloat(aWriter, cValue.Value);
    }
}

void NotifyGlobals::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    if (cCount > kMaxGlobals)
        return;
    Values.resize(cCount);
    for (auto& value : Values)
    {
        value.Id.Deserialize(aReader);
        value.Value = Serialization::ReadFloat(aReader);
    }
}

void NotifyQuests::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, Join);
    Serialization::WriteVarInt(aWriter, Quests.size());
    for (const auto& cQuest : Quests)
        Write(aWriter, cQuest);
}

void NotifyQuests::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Join = Serialization::ReadBool(aReader);
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    if (cCount > kMaxQuests)
        return;
    Quests.resize(cCount);
    for (auto& quest : Quests)
        if (!Read(aReader, quest))
        {
            Quests.clear();
            return;
        }
}

void NotifyScriptResults::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Results.size());
    for (const auto& cResult : Results)
    {
        Serialization::WriteVarInt(aWriter, static_cast<uint8_t>(cResult.Type));
        cResult.Object.Serialize(aWriter);
        if (cResult.Type == ScriptResult::Kind::kActivate)
        {
            cResult.Activator.Serialize(aWriter);
            aWriter.WriteBits(cResult.OpenState, 8);
        }
        else
        {
            Serialization::WriteString(aWriter, cResult.Animation);
            Serialization::WriteString(aWriter, cResult.Event);
        }
    }
}

void NotifyScriptResults::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    if (cCount > kMaxScriptResults)
        return;
    Results.resize(cCount);
    for (auto& result : Results)
    {
        result.Type = static_cast<ScriptResult::Kind>(Serialization::ReadVarInt(aReader));
        result.Object.Deserialize(aReader);
        if (result.Type == ScriptResult::Kind::kActivate)
        {
            result.Activator.Deserialize(aReader);
            uint64_t openState = 0;
            aReader.ReadBits(openState, 8);
            result.OpenState = static_cast<uint8_t>(openState);
        }
        else
        {
            result.Animation = Serialization::ReadString(aReader);
            result.Event = Serialization::ReadString(aReader);
        }
    }
}
