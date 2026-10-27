#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Inventory.h>

#include <cstring>
#include <Wire/Buffer.h>

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
struct NotifyGlobals final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyGlobals;
    NotifyGlobals() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyGlobals& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Values == acRhs.Values; }

    std::vector<GlobalValue> Values;
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
// A reference alias's fill. A reference the host's game created goes by its session id (ObjectChange::kCreatedModId)
// once it went out; one never sent is HostOnly (a guest leaves the role). A plugin's names where it lives (Cell: its
// cell, or outdoors its worldspace and grid cell) for a guest whose game has to read it from the plugin.
struct QuestAliasFill
{
    uint32_t AliasId{};
    GameId Ref{};
    bool HostOnly{};
    GameId Cell{};
    int16_t GridX{};
    int16_t GridY{};

    bool operator==(const QuestAliasFill&) const noexcept = default;
};

// A location alias's fill (none: cleared).
struct QuestLocationFill
{
    uint32_t AliasId{};
    GameId Location{};

    bool operator==(const QuestLocationFill&) const noexcept = default;
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
    std::vector<uint16_t> DoneStages;
    std::vector<QuestObjective> Objectives;
    std::vector<QuestAliasFill> Aliases;
    std::vector<QuestLocationFill> Locations;

    bool operator==(const QuestState& acRhs) const noexcept
    {
        return Id == acRhs.Id && Flags == acRhs.Flags && CurrentStage == acRhs.CurrentStage && DoneStages == acRhs.DoneStages &&
               Objectives == acRhs.Objectives && Aliases == acRhs.Aliases && Locations == acRhs.Locations;
    }
};

// The host's quests that changed; a joining player gets every quest (Join: applied as a save load does).
struct NotifyQuests final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyQuests;
    NotifyQuests() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyQuests& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Quests == acRhs.Quests && Join == acRhs.Join;
    }

    std::vector<QuestState> Quests;
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
        kItems, // a container holds Value of Item now, Stacks those of them with extra data
        // States, from here on: Value.
        kEnabled,
        kLock,
        kSaveData, // Value: the change flags, Data: what the reference's save holds for them (FormState)
        kOpenState, // Value: 1 open
        kMove,      // Data: position and angle, 6 floats
        kScale,     // Value: percent
        kOwner,     // Item: the owner, none for no owner
        kContents,  // Data: every item a container holds (ItemCount each, PackContents), the host's leveled roll;
                    // Stacks: those with extra data
        kBase,      // Item: its base, the host's pick for one placed with a leveled base
        kCreate,    // the host's game made Object (kCreatedModId): Item its base, Data where (PackPlacement);
                    // an item's count and extra data in Stacks (one)
        kRecord,    // Value: the row flags the host's change record holds; a guest's record is made the same
        kGraph,     // Data: its behaviour graphs' state as a save keeps it (a lever's, a claw ring's), at a cell load
    };

    // The mod id of a reference the host's game created (its 0xFF id's low bits as the base id): each game makes its
    // own, mapped by the guest.
    static constexpr uint32_t kCreatedModId = 0xFFFFFFFF;

    // Where a created reference goes: its cell, or its worldspace outdoors (either may be none), position and angle.
    struct Placement
    {
        GameId Cell{};
        GameId WorldSpace{};
        float Position[3]{};
        float Rotation[3]{};

        bool operator==(const Placement&) const noexcept = default;
    };

    static std::string PackPlacement(const Placement& acPlacement) noexcept
    {
        const uint32_t cIds[4] = {acPlacement.Cell.ModId, acPlacement.Cell.BaseId, acPlacement.WorldSpace.ModId, acPlacement.WorldSpace.BaseId};
        std::string data(sizeof(cIds) + 6 * sizeof(float), '\0');
        std::memcpy(data.data(), cIds, sizeof(cIds));
        std::memcpy(data.data() + sizeof(cIds), acPlacement.Position, sizeof(acPlacement.Position));
        std::memcpy(data.data() + sizeof(cIds) + sizeof(acPlacement.Position), acPlacement.Rotation, sizeof(acPlacement.Rotation));
        return data;
    }

    static bool UnpackPlacement(const std::string& acData, Placement& aPlacement) noexcept
    {
        uint32_t ids[4];
        if (acData.size() != sizeof(ids) + 6 * sizeof(float))
            return false;
        std::memcpy(ids, acData.data(), sizeof(ids));
        aPlacement.Cell = GameId(ids[0], ids[1]);
        aPlacement.WorldSpace = GameId(ids[2], ids[3]);
        std::memcpy(aPlacement.Position, acData.data() + sizeof(ids), sizeof(aPlacement.Position));
        std::memcpy(aPlacement.Rotation, acData.data() + sizeof(ids) + sizeof(aPlacement.Position), sizeof(aPlacement.Rotation));
        return true;
    }

    // A container's item and how many it holds.
    struct ItemCount
    {
        GameId Item{};
        int32_t Count{};

        bool operator==(const ItemCount& acRhs) const noexcept { return Item == acRhs.Item && Count == acRhs.Count; }
    };

    // kContents' Data: mod id, base id and count, 32 bits each.
    static std::string PackContents(const std::vector<ItemCount>& acItems) noexcept
    {
        std::string data(acItems.size() * 3 * sizeof(uint32_t), '\0');
        auto* pOut = data.data();
        for (const auto& cItem : acItems)
        {
            const uint32_t cValues[3] = {cItem.Item.ModId, cItem.Item.BaseId, static_cast<uint32_t>(cItem.Count)};
            std::memcpy(pOut, cValues, sizeof(cValues));
            pOut += sizeof(cValues);
        }
        return data;
    }

    static std::vector<ItemCount> UnpackContents(const std::string& acData) noexcept
    {
        std::vector<ItemCount> items(acData.size() / (3 * sizeof(uint32_t)));
        for (size_t i = 0; i < items.size(); ++i)
        {
            uint32_t values[3];
            std::memcpy(values, acData.data() + i * sizeof(values), sizeof(values));
            items[i] = {GameId(values[0], values[1]), static_cast<int32_t>(values[2])};
        }
        return items;
    }

    // The kinds that carry Data, and those that carry Item.
    static bool HasData(Kind aKind) noexcept
    {
        return aKind == Kind::kSaveData || aKind == Kind::kMove || aKind == Kind::kContents || aKind == Kind::kCreate ||
               aKind == Kind::kGraph;
    }
    static bool HasItem(Kind aKind) noexcept
    {
        return aKind == Kind::kItems || aKind == Kind::kOwner || aKind == Kind::kBase || aKind == Kind::kCreate;
    }
    // The kinds that carry items' extra data (enchantment, tempering, a name...).
    static bool HasStacks(Kind aKind) noexcept
    {
        return aKind == Kind::kItems || aKind == Kind::kContents || aKind == Kind::kCreate;
    }

    Kind Type{};
    GameId Object{};
    GameId Activator{};              // kActivate: the reference that activated it
    uint8_t OpenState{};             // kActivate: the door's open state before
    std::string Animation; // kAnimation: PlayAnimationAndWait's, empty for PlayAnimation
    std::string Event;     // kAnimation
    GameId Item{};                   // kItems: the base item; kOwner: the owner; kBase: the base
    uint32_t Value{};                // a state's; kItems: the count
    std::string Data;      // kSaveData, kMove, kContents
    std::vector<Inventory::Entry> Stacks; // HasStacks: items with extra data, each its count

    bool operator==(const ObjectChange& acRhs) const noexcept
    {
        return Type == acRhs.Type && Object == acRhs.Object && Activator == acRhs.Activator && OpenState == acRhs.OpenState &&
               Animation == acRhs.Animation && Event == acRhs.Event && Item == acRhs.Item && Value == acRhs.Value && Data == acRhs.Data &&
               Stacks == acRhs.Stacks;
    }
};

// The host's world objects' changes since the last, in the order its game had them; or, with Cell, the state of that
// cell's references a guest asked for when it loaded (CellStateRequest).
struct NotifyObjectChanges final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyObjectChanges;
    NotifyObjectChanges() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyObjectChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Cell == acRhs.Cell && Changes == acRhs.Changes;
    }

    // Set: that cell's state, for a guest that has it loaded.
    GameId Cell{};
    std::vector<ObjectChange> Changes;
};
