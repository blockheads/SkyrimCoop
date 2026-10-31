// Party items (Games/PartyItems.h, docs/design/party-items.md).
#include <catch2/catch.hpp>

#include <Games/PartyItems.h>

TEST_CASE("Keys, quest items and the story's items arrive as the party's; loot does not", "[PartyItems]")
{
    CHECK(PartyItems::ArrivesAsParty(PartyItems::cKeyFormType, false, false));
    CHECK(PartyItems::ArrivesAsParty(41, true, false));  // a quest item
    CHECK(PartyItems::ArrivesAsParty(41, false, true));  // a quest's script gave it
    CHECK_FALSE(PartyItems::ArrivesAsParty(41, false, false)); // a weapon picked up
}

TEST_CASE("A party stack is a tagged or quest one; its tag goes in front once", "[PartyItems]")
{
    Inventory::Entry item;
    CHECK_FALSE(PartyItems::IsParty(item));
    item.ExtraName = "[P] Iron Sword";
    CHECK(PartyItems::IsParty(item));
    item.ExtraName.clear();
    item.IsQuestItem = true;
    CHECK(PartyItems::IsParty(item));
    CHECK(PartyItems::Tagged("", "Iron Sword") == "[P] Iron Sword");
    CHECK(PartyItems::Tagged("Old Friend", "Iron Sword") == "[P] Old Friend");
    CHECK(PartyItems::Tagged("[P] Old Friend", "Iron Sword") == "[P] Old Friend");
}

TEST_CASE("A carried stack is a party item by base and tag; a party quest item is the engine's quest stack too", "[PartyItems]")
{
    Inventory::Entry sword;
    sword.BaseId = GameId(0, 0x12EB7);
    sword.ExtraName = "[P] Iron Sword";
    Inventory::Entry carried = sword;
    CHECK(PartyItems::SameStack(sword, carried));
    carried.ExtraName = "Iron Sword";
    CHECK_FALSE(PartyItems::SameStack(sword, carried)); // a player's own
    carried.ExtraName = "[P] Iron Sword";
    carried.BaseId = GameId(0, 0x12EB8);
    CHECK_FALSE(PartyItems::SameStack(sword, carried));

    Inventory::Entry claw;
    claw.BaseId = GameId(0, 0x39647);
    claw.IsQuestItem = true;
    claw.ExtraName = "[P] Golden Claw";
    Inventory::Entry held = claw; // the holder's: the engine's quest stack, its alias's name
    held.ExtraName.clear();
    CHECK(PartyItems::SameStack(claw, held));
    Inventory::Entry copy = claw; // every other player's: a tagged plain copy
    copy.IsQuestItem = false;
    CHECK(PartyItems::SameStack(claw, copy));
    copy.ExtraName.clear();
    CHECK_FALSE(PartyItems::SameStack(claw, copy)); // a plain claw of a player's own
    CHECK_FALSE(PartyItems::SameStack(sword, held));
}

TEST_CASE("An improved party stack is another party stack; a copy's own use is not", "[PartyItems]")
{
    Inventory::Entry sword;
    sword.BaseId = GameId(0, 0x12EB7);
    sword.ExtraName = "[P] Iron Sword";
    Inventory::Entry tempered = sword;
    tempered.ExtraHealth = 1.2f;
    CHECK_FALSE(PartyItems::SameStack(sword, tempered));
    Inventory::Entry enchanted = sword;
    enchanted.ExtraEnchantId = GameId(0, 0x4605A);
    enchanted.ExtraEnchantCharge = 1000;
    CHECK_FALSE(PartyItems::SameStack(sword, enchanted));
    Inventory::Entry used = enchanted; // charge spent hitting, a poison applied: this copy's own
    used.ExtraCharge = 300.f;
    used.ExtraPoisonId = GameId(0, 0x3A5A4);
    CHECK(PartyItems::SameStack(enchanted, used));
}
