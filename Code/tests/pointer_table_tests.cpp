#include <catch2/catch.hpp>
#include "game_bridge/pointer_table.h"

TEST_CASE("PointerTable_InsertLookup", "[PointerTable]") {
    PointerTable table;
    table.Insert(0x14, 0xDEAD);
    REQUIRE(table.Lookup(0x14) == 0xDEAD);
    REQUIRE(table.Contains(0x14));
    REQUIRE(table.Size() == 1);
}

TEST_CASE("PointerTable_LookupMissing", "[PointerTable]") {
    PointerTable table;
    REQUIRE(table.Lookup(0x99) == 0);
    REQUIRE_FALSE(table.Contains(0x99));
}

TEST_CASE("PointerTable_Remove", "[PointerTable]") {
    PointerTable table;
    table.Insert(0x14, 0xDEAD);
    REQUIRE(table.Contains(0x14));
    table.Remove(0x14);
    REQUIRE(table.Lookup(0x14) == 0);
    REQUIRE_FALSE(table.Contains(0x14));
    REQUIRE(table.Size() == 0);
}

TEST_CASE("PointerTable_RemoveByPointer", "[PointerTable]") {
    PointerTable table;
    table.Insert(0x01, 0xAAAA);
    table.Insert(0x02, 0xBBBB);
    table.Insert(0x03, 0xCCCC);

    table.RemoveByPointer(0xBBBB);

    REQUIRE(table.Contains(0x01));
    REQUIRE_FALSE(table.Contains(0x02));
    REQUIRE(table.Contains(0x03));
    REQUIRE(table.Size() == 2);
}

TEST_CASE("PointerTable_Clear", "[PointerTable]") {
    PointerTable table;
    table.Insert(0x01, 0xA);
    table.Insert(0x02, 0xB);
    table.Insert(0x03, 0xC);
    table.Insert(0x04, 0xD);
    table.Insert(0x05, 0xE);

    REQUIRE(table.Size() == 5);
    table.Clear();
    REQUIRE(table.Size() == 0);
}

TEST_CASE("PointerTable_ForEach", "[PointerTable]") {
    PointerTable table;
    table.Insert(0x01, 0xA);
    table.Insert(0x02, 0xB);
    table.Insert(0x03, 0xC);

    size_t count = 0;
    table.ForEach([&count](uint32_t, uint64_t) {
        count++;
    });
    REQUIRE(count == 3);
}

TEST_CASE("PointerTable_OverwriteExisting", "[PointerTable]") {
    PointerTable table;
    table.Insert(0x14, 0xAAAA);
    table.Insert(0x14, 0xBBBB);
    REQUIRE(table.Lookup(0x14) == 0xBBBB);
    REQUIRE(table.Size() == 1);
}
