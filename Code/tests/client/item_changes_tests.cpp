// An actor's stacks before and after an in-place change, as item changes (Games/ItemChanges.h).
#include <catch2/catch.hpp>

#include <Games/ItemChanges.h>

TEST_CASE("Tempering one of two swords: one plain sword fewer, one tempered more", "[ItemChanges]")
{
    Inventory::Entry sword;
    sword.BaseId = GameId(0, 0x12EB7);
    sword.ExtraName = "[P] Iron Sword";
    sword.Count = 2;
    Inventory::Entry plain = sword, tempered = sword;
    plain.Count = 1;
    tempered.Count = 1;
    tempered.ExtraHealth = 1.2f;
    const auto cChanges = ItemChanges::Diff({sword}, {plain, tempered});
    REQUIRE(cChanges.size() == 2);
    CHECK(cChanges[0].ExtraHealth == 0.f);
    CHECK(cChanges[0].Count == -1);
    CHECK(cChanges[1].ExtraHealth == 1.2f);
    CHECK(cChanges[1].Count == 1);
    CHECK(ItemChanges::Diff({sword}, {sword}).empty());
}

TEST_CASE("A removal names the carried stack with the same extra data, worn or not", "[ItemChanges]")
{
    Inventory::Entry plain, tempered, named;
    plain.BaseId = tempered.BaseId = named.BaseId = GameId(0, 0x12EB7);
    tempered.ExtraHealth = 1.2f;
    tempered.ExtraWorn = true;
    named.ExtraName = "[P] Iron Sword";
    const std::vector<Inventory::Entry> cCarried{plain, tempered, named};
    Inventory::Entry asked = named;
    asked.Count = -1;
    CHECK(ItemChanges::FindStack(cCarried, asked) == 2u);
    asked = tempered;
    asked.ExtraWorn = false;
    CHECK(ItemChanges::FindStack(cCarried, asked) == 1u);
    asked.ExtraHealth = 1.4f;
    CHECK_FALSE(ItemChanges::FindStack(cCarried, asked));
}
