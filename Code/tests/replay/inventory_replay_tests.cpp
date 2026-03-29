#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Inventory events increment event counter", "[Replay][InventoryService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("inventory_changes.ndjson"));

    REQUIRE(events == 5);

    // 4 events: add_inventory, equip, unequip, remove_inv_item
    // (character_ctor goes to CharacterService not InventoryService)
    REQUIRE(harness.GetInventoryService().GetEventCount() >= 4);

    CHECK(harness.GetPointerTable().Contains(0x14));
}
