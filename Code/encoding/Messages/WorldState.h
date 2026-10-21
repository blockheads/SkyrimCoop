#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// World state from the host's game (docs/design/world-state-sync.md, step 4).

struct GlobalValue
{
    GameId Id{};
    float Value{};

    bool operator==(const GlobalValue& acRhs) const noexcept { return Id == acRhs.Id && Value == acRhs.Value; }
};

// A global as the host's game reads it, not sent as is.
struct HostGlobal
{
    GameId Id{};
    float Value{};
    bool Clock{}; // the calendar's (GameHour, GameDaysPassed...): HostClock shares those
};

// The host's global variables that changed; a joining player gets all of them.
struct NotifyGlobals final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyGlobals;
    NotifyGlobals() : ServerMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyGlobals& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Values == acRhs.Values; }

    TiltedPhoques::Vector<GlobalValue> Values;
};

// A quest's objective with a state other than hidden (0 hidden, 1 displayed, 2/3 completed, 4/5 failed).
struct QuestObjective
{
    uint16_t Index{};
    uint8_t State{};

    bool operator==(const QuestObjective& acRhs) const noexcept { return Index == acRhs.Index && State == acRhs.State; }
};

// What fills a reference alias: none, a reference (by id), or one the host's game created (Created: its id is
// that game's own, so it cannot be sent by id).
struct QuestAliasFill
{
    uint32_t AliasId{};
    GameId Ref{};
    bool Created{};

    bool operator==(const QuestAliasFill& acRhs) const noexcept
    {
        return AliasId == acRhs.AliasId && Ref == acRhs.Ref && Created == acRhs.Created;
    }
};

// A quest as the host's game holds it: flags Running (0x1), Completed (0x2), Failed (0x40), StageWait (0x80), the stage, the
// stages done (ascending), objectives, and every reference alias while it runs.
struct QuestState
{
    // The TESQuest flags it carries: running (0x1), completed (0x2), failed (0x40), starting (StageWait, 0x80).
    static constexpr uint16_t kFlags = 0x1 | 0x2 | 0x40 | 0x80;

    GameId Id{};
    uint16_t Flags{};
    uint16_t CurrentStage{};
    TiltedPhoques::Vector<uint16_t> DoneStages;
    TiltedPhoques::Vector<QuestObjective> Objectives;
    TiltedPhoques::Vector<QuestAliasFill> Aliases;

    bool operator==(const QuestState& acRhs) const noexcept
    {
        return Id == acRhs.Id && Flags == acRhs.Flags && CurrentStage == acRhs.CurrentStage && DoneStages == acRhs.DoneStages &&
               Objectives == acRhs.Objectives && Aliases == acRhs.Aliases;
    }
};

// The host's quests that changed; a joining player gets every quest (Join: applied as a save load does).
struct NotifyQuests final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyQuests;
    NotifyQuests() : ServerMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyQuests& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Quests == acRhs.Quests && Join == acRhs.Join;
    }

    TiltedPhoques::Vector<QuestState> Quests;
    bool Join{};
};
