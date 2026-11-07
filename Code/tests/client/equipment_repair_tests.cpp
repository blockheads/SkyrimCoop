#include <catch2/catch.hpp>
#include <Games/EquipmentRepair.h>
#include <cstdint>
#include <set>
#include <utility>

namespace
{
using Worn = std::set<std::pair<uint32_t, bool>>;
struct Equipment
{
    Worn Current;
    bool Stuck = false;
    uint32_t HandSpell = 0;
    uint32_t Resets = 0;
    uint32_t Equips = 0;

    void Restore(const Worn& acWanted, uint32_t aHandSpell = 0)
    {
        const auto cBefore = Current;
        EquipmentRepair::Apply(acWanted, cBefore, HandSpell == aHandSpell, [&] {
            ++Resets;
            Current.clear();
            HandSpell = aHandSpell;
            Stuck = false;
        }, [&](uint32_t aId, bool aLeft) {
            ++Equips;
            if (!Stuck)
                Current.emplace(aId, aLeft);
        });
    }
};
}

TEST_CASE("A partial outfit stuck after a model rebuild recovers together", "[equipment-repair]")
{
    const Worn cWanted{{1, false}, {2, false}, {3, true}};
    Equipment gear{{{1, false}}, true};
    gear.Restore(cWanted);
    CHECK(gear.Current == cWanted);
    gear.Restore(cWanted);
    CHECK(gear.Resets == 1);
    CHECK(gear.Equips == 3);
}

TEST_CASE("A replacement outfit removes the copy's extra pieces", "[equipment-repair]")
{
    const Worn cWanted{{2, false}, {3, true}};
    Equipment gear{{{1, false}, {3, false}}};
    gear.Restore(cWanted);
    CHECK(gear.Current == cWanted);
}

TEST_CASE("A matching outfit remains untouched", "[equipment-repair]")
{
    const Worn cWanted{{1, false}, {3, true}};
    Equipment gear{cWanted};
    gear.Restore(cWanted);
    CHECK(gear.Current == cWanted);
    CHECK(gear.Resets == 0);
    CHECK(gear.Equips == 0);
}

TEST_CASE("A copy's hand spell dropped by a move comes back with its outfit", "[equipment-repair][hand-spell]")
{
    const Worn cWanted{{1, false}};
    Equipment gear{cWanted};
    gear.Restore(cWanted, 0x12FCC);
    CHECK(gear.HandSpell == 0x12FCC);
    CHECK(gear.Current == cWanted);
    CHECK(gear.Resets == 1);
    gear.Restore(cWanted, 0x12FCC);
    CHECK(gear.Resets == 1);
}

TEST_CASE("A hand that only selects its owner's spell is repaired; one holding a weapon is not", "[equipment-repair][hand-spell]")
{
    CHECK_FALSE(EquipmentRepair::HandHolds(0x12FCC, 0)); // selected, the hand empty
    CHECK(EquipmentRepair::HandHolds(0x12FCC, 0x12FCC));
    CHECK(EquipmentRepair::HandHolds(0x12FCC, 0x13989)); // a weapon over a stale selection: no reset each 3 s
    CHECK(EquipmentRepair::HandHolds(0, 0));
}

TEST_CASE("Taking everything off clears a copy's old worn set", "[equipment-repair]")
{
    Equipment gear{{{1, false}, {3, true}}};
    gear.Restore({});
    CHECK(gear.Current.empty());
    CHECK(gear.Resets == 1);
    CHECK(gear.Equips == 0);
}

TEST_CASE("Equipment changes coalesce into the next snapshot while quiet actors keep the heartbeat", "[equipment-repair]")
{
    using namespace std::chrono_literals;
    EquipmentRepair::Snapshots snapshots;
    const auto cStart = EquipmentRepair::Snapshots::Clock::time_point{} + 10s;
    REQUIRE(snapshots.SendAt(cStart));
    CHECK_FALSE(snapshots.SendAt(cStart + 100ms));
    snapshots.OnChange();
    snapshots.OnChange();
    CHECK(snapshots.SendAt(cStart + 200ms));
    CHECK_FALSE(snapshots.SendAt(cStart + 201ms));
    CHECK_FALSE(snapshots.SendAt(cStart + 3199ms));
    CHECK(snapshots.SendAt(cStart + 3200ms));
}

TEST_CASE("Non-weapons retain their native equip slot during repair", "[equipment-repair][repair-slot]")
{
    CHECK(EquipmentRepair::EquipSlot(26, false) == 0); // armor/shields
    CHECK(EquipmentRepair::EquipSlot(31, false) == 0); // torch
    CHECK(EquipmentRepair::EquipSlot(42, false) == 0); // arrows
    CHECK(EquipmentRepair::EquipSlot(41, false) == 0x13F42);
    CHECK(EquipmentRepair::EquipSlot(41, true) == 0x13F43);
}
