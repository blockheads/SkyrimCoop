#pragma once

#include "Message.h"

#include <Structs/GameId.h>

#include <cstring>

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

// A world object changing on the host's game in a way no other message carries (steps 5 and 6): a script's event (a
// door it activated by a reference, `Activate(Self)`, or an animation it played), a container's count of an item, or a
// reference's state, as one value its row packs (client `Games/ReferenceStates.cpp`).
struct ObjectChange
{
    enum class Kind : uint8_t
    {
        kActivate,
        kAnimation,
        kItems, // a container holds Value of Item now
        // States, from here on: Value.
        kEnabled,
        kLock,
        kSaveData, // Value: the change flags, Data: what the reference's save holds for them (FormState)
        kOpenState, // Value: 1 open
        kMove,      // Data: position and angle, 6 floats
        kScale,     // Value: percent
        kOwner,     // Item: the owner, none for no owner
        kContents,  // Data: every item a container holds (ItemCount each, PackContents), the host's leveled roll
    };

    // A container's item and how many it holds.
    struct ItemCount
    {
        GameId Item{};
        int32_t Count{};

        bool operator==(const ItemCount& acRhs) const noexcept { return Item == acRhs.Item && Count == acRhs.Count; }
    };

    // kContents' Data: mod id, base id and count, 32 bits each.
    static TiltedPhoques::String PackContents(const TiltedPhoques::Vector<ItemCount>& acItems) noexcept
    {
        TiltedPhoques::String data(acItems.size() * 3 * sizeof(uint32_t), '\0');
        auto* pOut = data.data();
        for (const auto& cItem : acItems)
        {
            const uint32_t cValues[3] = {cItem.Item.ModId, cItem.Item.BaseId, static_cast<uint32_t>(cItem.Count)};
            std::memcpy(pOut, cValues, sizeof(cValues));
            pOut += sizeof(cValues);
        }
        return data;
    }

    static TiltedPhoques::Vector<ItemCount> UnpackContents(const TiltedPhoques::String& acData) noexcept
    {
        TiltedPhoques::Vector<ItemCount> items(acData.size() / (3 * sizeof(uint32_t)));
        for (size_t i = 0; i < items.size(); ++i)
        {
            uint32_t values[3];
            std::memcpy(values, acData.data() + i * sizeof(values), sizeof(values));
            items[i] = {GameId(values[0], values[1]), static_cast<int32_t>(values[2])};
        }
        return items;
    }

    // The kinds that carry Data, and those that carry Item.
    static bool HasData(Kind aKind) noexcept { return aKind == Kind::kSaveData || aKind == Kind::kMove || aKind == Kind::kContents; }
    static bool HasItem(Kind aKind) noexcept { return aKind == Kind::kItems || aKind == Kind::kOwner; }

    Kind Type{};
    GameId Object{};
    GameId Activator{};              // kActivate: the reference that activated it
    uint8_t OpenState{};             // kActivate: the door's open state before
    TiltedPhoques::String Animation; // kAnimation: PlayAnimationAndWait's, empty for PlayAnimation
    TiltedPhoques::String Event;     // kAnimation
    GameId Item{};                   // kItems: the base item; kOwner: the owner
    uint32_t Value{};                // a state's; kItems: the count
    TiltedPhoques::String Data;      // kSaveData, kMove, kContents

    bool operator==(const ObjectChange& acRhs) const noexcept
    {
        return Type == acRhs.Type && Object == acRhs.Object && Activator == acRhs.Activator && OpenState == acRhs.OpenState &&
               Animation == acRhs.Animation && Event == acRhs.Event && Item == acRhs.Item && Value == acRhs.Value && Data == acRhs.Data;
    }
};

// The host's world objects' changes since the last, in the order its game had them; or, with Cell, the state of that
// cell's references a guest asked for when it loaded (CellStateRequest).
struct NotifyObjectChanges final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyObjectChanges;
    NotifyObjectChanges() : ServerMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyObjectChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Cell == acRhs.Cell && Changes == acRhs.Changes;
    }

    // Set: that cell's state, for a guest that has it loaded.
    GameId Cell{};
    TiltedPhoques::Vector<ObjectChange> Changes;
};
