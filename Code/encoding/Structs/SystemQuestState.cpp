#include <Structs/SystemQuestState.h>
#include <Wire/Serialization.h>

#include <algorithm>

using Wire::Serialization;

namespace
{
constexpr uint32_t kVersion = 1;
constexpr uint64_t kMaxEntries = 4096;
} // namespace

const SystemQuestState::Quest* SystemQuestState::Find(const GameId& acId) const noexcept
{
    const auto it = std::ranges::find(Quests, acId, &Quest::Id);
    return it == Quests.end() ? nullptr : &*it;
}

std::string SystemQuestState::Encode() const noexcept
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    Serialization::WriteVarInt(writer, kVersion);
    Serialization::WriteVarInt(writer, Quests.size());
    for (const Quest& cQuest : Quests)
    {
        cQuest.Id.Serialize(writer);
        Serialization::WriteVarInt(writer, cQuest.Variables.size());
        for (const Variable& cVariable : cQuest.Variables)
        {
            Serialization::WriteString(writer, cVariable.Script);
            Serialization::WriteString(writer, cVariable.Name);
            SerializeScriptValue(writer, cVariable.Value);
        }
        Serialization::WriteVarInt(writer, cQuest.Timers.size());
        for (const Timer& cTimer : cQuest.Timers)
        {
            Serialization::WriteString(writer, cTimer.Native);
            Serialization::WriteFloat(writer, cTimer.Interval);
        }
    }
    Serialization::WriteVarInt(writer, Globals.size());
    for (const Global& cGlobal : Globals)
    {
        cGlobal.Id.Serialize(writer);
        Serialization::WriteFloat(writer, cGlobal.Value);
    }
    return {reinterpret_cast<const char*>(buffer.GetData()), writer.Size()};
}

std::optional<SystemQuestState> SystemQuestState::Decode(std::string_view aData) noexcept
{
    if (aData.empty())
        return std::nullopt;
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(aData.data()), aData.size()));
    if (Serialization::ReadVarInt(reader) != kVersion)
        return std::nullopt;
    SystemQuestState state;
    const uint64_t cQuests = Serialization::ReadVarInt(reader);
    if (cQuests > kMaxEntries)
        return std::nullopt;
    for (uint64_t i = 0; i < cQuests; ++i)
    {
        Quest& quest = state.Quests.emplace_back();
        quest.Id.Deserialize(reader);
        const uint64_t cVariables = Serialization::ReadVarInt(reader);
        if (cVariables > kMaxEntries)
            return std::nullopt;
        for (uint64_t j = 0; j < cVariables; ++j)
        {
            Variable& variable = quest.Variables.emplace_back();
            variable.Script = Serialization::ReadString(reader);
            variable.Name = Serialization::ReadString(reader);
            if (!DeserializeScriptValue(reader, variable.Value))
                return std::nullopt;
        }
        const uint64_t cTimers = Serialization::ReadVarInt(reader);
        if (cTimers > kMaxEntries)
            return std::nullopt;
        for (uint64_t j = 0; j < cTimers; ++j)
        {
            Timer& timer = quest.Timers.emplace_back();
            timer.Native = Serialization::ReadString(reader);
            timer.Interval = Serialization::ReadFloat(reader);
        }
    }
    const uint64_t cGlobals = Serialization::ReadVarInt(reader);
    if (cGlobals > kMaxEntries)
        return std::nullopt;
    for (uint64_t i = 0; i < cGlobals; ++i)
    {
        Global& global = state.Globals.emplace_back();
        global.Id.Deserialize(reader);
        global.Value = Serialization::ReadFloat(reader);
    }
    return state;
}
