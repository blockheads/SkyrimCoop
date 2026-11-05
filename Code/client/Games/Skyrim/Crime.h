#pragma once

#include <Games/Primitives.h>
#include <RuntimeVersion.h>
#include <cstddef>
#include <cstdint>

struct TESFaction;
struct TESObjectREFR;

// Crime layouts, checked against the native entries in shared-party-bounty.md; the player's shift on 1.7.104 (40641, 40655).
namespace CrimeEngine
{
struct Gold
{
    uint32_t Violent, NonViolent, NonViolentInfamy, ViolentInfamy;
};
struct GoldEntry
{
    TESFaction* Faction;
    Gold Value;
    GoldEntry* Next;
};
struct GoldMap
{
    uint32_t Unused, Capacity, FreeCount, FreeOffset;
    GoldEntry* End;
    uintptr_t Unknown;
    GoldEntry* Entries;
};
struct Player
{
    static constexpr size_t cShift = SkyrimRuntime::PlayerShift;
    uint8_t pad0[0x3F0 + cShift];
    GoldMap Bounty;
    uint8_t pad418[0x6D0 - 0x418];
    uint32_t TheftWarnings; // StealAlarm warns below 2 and adds one (crime.md)
    uint8_t pad6D4[0x720 - 0x6D4];
    TESFaction* JailFaction;
    int32_t JailSentence;
    uint8_t pad72C[0xBE4 - 0x72C];
    uint8_t Flags4; // PlayerFlags byte 4; bit 2: Game.SetPlayerReportCrime (55578), StealAlarm's gate
};
struct Faction
{
    uint8_t pad0[0x58];
    uint32_t Flags;
    uint32_t pad5C;
    TESObjectREFR* JailMarker;
    TESObjectREFR* WaitMarker;
    TESObjectREFR* StolenContainer;
    TESObjectREFR* InventoryContainer;
};
struct DataHandler
{
    uint8_t pad0[0x118];
    GameArray<TESFaction*> Factions;
};
struct ContainerMenu
{
    uint8_t pad0[0xB0];
    uint32_t StealValue;
};
// A witness's detection of one target (ActorKnowledge 42699, DetectionState), read by RequestDetectionLevel 37764.
struct DetectionState
{
    uint8_t pad0[0x10];
    int32_t Level;
    uint8_t pad14[0x38 - 0x14];
    float UpdatedAt; // elapsed time (404125) of the last detection pass
};
struct ActorKnowledge
{
    uint8_t pad0[0x10];
    uint32_t Witness, Target; // ref handles
    uint8_t pad18[0x20 - 0x18];
    DetectionState* State; // null until a detection pass answers a request
    uint8_t pad28[0x44 - 0x28];
    float RequestedAt; // -FLT_MAX when no update is queued (42712)
};
// A running alarm (TESPackage type 21): the crimes it answers, first at +0xE0, read by 39651.
struct AlarmPackage
{
    static constexpr uint8_t cType = 21;
    uint8_t pad0[0x24];
    uint8_t Type;
    uint8_t pad25[0xE0 - 0x25];
    void* Crime;
};
struct LockpickMenu
{
    uint8_t pad0[0x10D]{};
    bool Reported{};
};
static_assert(sizeof(GoldEntry) == 0x20);
static_assert(offsetof(GoldEntry, Next) == 0x18);
static_assert(sizeof(GoldMap) == 0x28);
static_assert(offsetof(GoldMap, Capacity) == 0x4);
static_assert(offsetof(GoldMap, Entries) == 0x20);
// Measured per runtime in 40641, 40655 and 37422, not derived from the shift.
static_assert(offsetof(Player, Bounty) == (COOP_SKYRIM_1_6_1170 ? 0x3F0 : 0x3F8));
static_assert(offsetof(Player, TheftWarnings) == (COOP_SKYRIM_1_6_1170 ? 0x6D0 : 0x6D8));
static_assert(offsetof(Player, JailFaction) == (COOP_SKYRIM_1_6_1170 ? 0x720 : 0x728));
static_assert(offsetof(Player, JailSentence) == (COOP_SKYRIM_1_6_1170 ? 0x728 : 0x730));
static_assert(offsetof(Player, Flags4) == (COOP_SKYRIM_1_6_1170 ? 0xBE4 : 0xBEC));
static_assert(offsetof(Faction, Flags) == 0x58);
static_assert(offsetof(Faction, JailMarker) == 0x60);
static_assert(offsetof(Faction, StolenContainer) == 0x70);
static_assert(offsetof(DataHandler, Factions) == 0x118);
static_assert(offsetof(ContainerMenu, StealValue) == 0xB0);
static_assert(offsetof(LockpickMenu, Reported) == 0x10D);
static_assert(offsetof(AlarmPackage, Type) == 0x24);
static_assert(offsetof(AlarmPackage, Crime) == 0xE0);
static_assert(offsetof(DetectionState, Level) == 0x10);
static_assert(offsetof(DetectionState, UpdatedAt) == 0x38);
static_assert(offsetof(ActorKnowledge, Target) == 0x14);
static_assert(offsetof(ActorKnowledge, State) == 0x20);
static_assert(offsetof(ActorKnowledge, RequestedAt) == 0x44);
} // namespace CrimeEngine
