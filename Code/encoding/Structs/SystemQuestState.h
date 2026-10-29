#pragma once

#include <Structs/GameId.h>
#include <Structs/ScriptValue.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A player's own instances of the system quests (docs/design/system-quests.md), as its record keeps them
// (PlayerRecord::Extras[kExtra]): each quest's scripts' variables and its update registrations, and the globals the
// quests' scripts hold.
struct SystemQuestState
{
    static constexpr const char* kExtra = "system-quests";

    struct Variable
    {
        bool operator==(const Variable&) const noexcept = default;

        std::string Script;
        std::string Name;
        ScriptValue Value;
    };

    // A registration as its native set it (RegisterForUpdateGameTime, 12).
    struct Timer
    {
        bool operator==(const Timer&) const noexcept = default;

        std::string Native;
        float Interval{};
    };

    struct Quest
    {
        bool operator==(const Quest&) const noexcept = default;

        GameId Id{};
        std::vector<Variable> Variables;
        std::vector<Timer> Timers;
    };

    struct Global
    {
        bool operator==(const Global&) const noexcept = default;

        GameId Id{};
        float Value{};
    };

    bool operator==(const SystemQuestState&) const noexcept = default;

    [[nodiscard]] const Quest* Find(const GameId& acId) const noexcept;

    [[nodiscard]] std::string Encode() const noexcept;
    // None for a blob that is not one.
    [[nodiscard]] static std::optional<SystemQuestState> Decode(std::string_view aData) noexcept;

    std::vector<Quest> Quests;
    std::vector<Global> Globals;
};
