#pragma once

#include <Games/Primitives.h>
#include <cstddef>
#include <cstdint>

struct TESFaction;
struct TESObjectREFR;

// AE 1.6.1170 crime layouts, checked against the native entries in shared-party-bounty.md.
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
    uint8_t pad0[0x3F0];
    GoldMap Bounty;
    uint8_t pad418[0x720 - 0x418];
    TESFaction* JailFaction;
    int32_t JailSentence;
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
static_assert(offsetof(Player, Bounty) == 0x3F0);
static_assert(offsetof(Player, JailFaction) == 0x720);
static_assert(offsetof(Player, JailSentence) == 0x728);
static_assert(offsetof(Faction, Flags) == 0x58);
static_assert(offsetof(Faction, JailMarker) == 0x60);
static_assert(offsetof(Faction, StolenContainer) == 0x70);
static_assert(offsetof(DataHandler, Factions) == 0x118);
static_assert(offsetof(ContainerMenu, StealValue) == 0xB0);
static_assert(offsetof(LockpickMenu, Reported) == 0x10D);
} // namespace CrimeEngine
