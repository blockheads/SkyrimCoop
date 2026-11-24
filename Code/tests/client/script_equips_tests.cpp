// A script's EquipItem adding a missing item (Games/ScriptEquips.h, Actor.EquipItem 54661).
#include <catch2/catch.hpp>

#include <Games/ScriptEquips.h>

TEST_CASE("A script's EquipItem reports the item it added, the story's when a quest called it", "[ScriptEquips]")
{
    const GameId cClothes(0, 0x86991);

    SECTION("an added item: worn, and the story's from a quest's script")
    {
        const auto cEvent = ScriptEquips::Arrival(0x14, cClothes, 0, 1, true);
        REQUIRE(cEvent);
        CHECK(cEvent->FormId == 0x14);
        CHECK(cEvent->Item.BaseId == cClothes);
        CHECK(cEvent->Item.Count == 1);
        CHECK(cEvent->Item.ExtraWorn);
        CHECK(cEvent->StoryGiven);
        CHECK(cEvent->StoryWorn);
    }
    SECTION("from another script: its own, not the story's")
    {
        const auto cEvent = ScriptEquips::Arrival(0x14, cClothes, 0, 1, false);
        REQUIRE(cEvent);
        CHECK_FALSE(cEvent->StoryGiven);
        CHECK_FALSE(cEvent->StoryWorn);
    }
    SECTION("one it held already, or none it could add: nothing arrived")
    {
        CHECK_FALSE(ScriptEquips::Arrival(0x14, cClothes, 1, 1, true));
        CHECK_FALSE(ScriptEquips::Arrival(0x14, cClothes, 0, 0, true));
        CHECK_FALSE(ScriptEquips::Arrival(0x14, GameId{}, 0, 1, true));
    }
}
