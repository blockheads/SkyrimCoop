// FormIds: a form's id on the wire. Every game in a session loads the same plugins in the same order, so a form
// keeps its number; a created form (0xFF) is only its own game's.
#include <catch2/catch.hpp>

#include <FormIds.h>

TEST_CASE("Form ids: a plugin's form has the same id in every game", "[FormIds]")
{
    CHECK(ToSession(0x00012EB7) == GameId(0, 0x12EB7));
    CHECK(ToGame(GameId(0, 0x12EB7)) == 0x00012EB7);
    // A light plugin's form keeps its light index.
    CHECK(ToGame(*ToSession(0xFE005801)) == 0xFE005801);
    // No form is no id.
    CHECK(ToSession(0) == GameId{});
    CHECK(ToGame(GameId{}) == 0);
}

TEST_CASE("Form ids: a created form has none, unless a message rebuilds it", "[FormIds]")
{
    CHECK_FALSE(ToSession(0xFF000801).has_value());
    CHECK(ToSessionOrCreated(0xFF000801) == GameId(0xFFFFFFFF, 0x801));
    CHECK(ToSessionOrCreated(0x01000D62) == ToSession(0x01000D62));
    // Another game's created form is not this game's of the same number.
    CHECK(ToGame(CreatedId(0xFF000801)) == 0);
    CHECK(ToGame(GameId(0xFF, 0x801)) == 0);
}
