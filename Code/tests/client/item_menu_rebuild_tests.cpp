#include <catch2/catch.hpp>

#include <Games/ItemMenuRebuild.h>

TEST_CASE("An open item menu is rebuilt after a stack it lists changes", "[inventory][item-menu]")
{
    constexpr uint32_t cCorpse = 0xFF00083A, cPlayer = 0x14, cVendorChest = 0xA1B2C, cOther = 0x1234;

    SECTION("a loot menu lists its target's and the player's")
    {
        const ItemMenuView cLoot{.Open = true, .Lists = {cCorpse, cPlayer}};
        CHECK(MustRebuildFirst(cLoot, cCorpse));
        CHECK(MustRebuildFirst(cLoot, cPlayer));
        CHECK_FALSE(MustRebuildFirst(cLoot, cOther)); // an NPC elsewhere loses an arrow: the menu stays as it is
        CHECK_FALSE(MustRebuildFirst(cLoot, 0));
    }
    SECTION("barter lists the merchant inventory's owner and the player's, not the merchant's own")
    {
        const ItemMenuView cBarter{.Open = true, .Lists = {cVendorChest, cPlayer}};
        CHECK(MustRebuildFirst(cBarter, cVendorChest));
        CHECK(MustRebuildFirst(cBarter, cPlayer));
        CHECK_FALSE(MustRebuildFirst(cBarter, cOther));
    }
    SECTION("the inventory menu lists the player's only")
    {
        const ItemMenuView cInventory{.Open = true, .Lists = {0, cPlayer}};
        CHECK(MustRebuildFirst(cInventory, cPlayer));
        CHECK_FALSE(MustRebuildFirst(cInventory, cOther));
        CHECK_FALSE(MustRebuildFirst(cInventory, 0)); // its empty slot names no holder
    }
    SECTION("a closed menu lists nothing")
    {
        CHECK_FALSE(MustRebuildFirst(ItemMenuView{.Lists = {cCorpse, cPlayer}}, cPlayer));
    }
}

TEST_CASE("A noted change rebuilds its menu once at the UI job, and none for a closed menu", "[inventory][item-menu]")
{
    ItemMenuChanges changes;
    CHECK_FALSE(changes.Take(0, true)); // nothing noted: the UI job leaves the menu as it is

    changes.Note(0);
    changes.Note(0); // two removals in one update: one rebuild reads both
    CHECK_FALSE(changes.Take(1, true)); // another menu's
    CHECK(changes.Take(0, true));
    CHECK_FALSE(changes.Take(0, true));

    changes.Note(2);
    CHECK_FALSE(changes.Take(2, false)); // closed before the UI job: nothing to rebuild
    CHECK_FALSE(changes.Take(2, true)); // and not later, on its next opening
}
