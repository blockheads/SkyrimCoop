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

TEST_CASE("Form ids: an exterior cell the game made goes as none; the worldspace and position place it", "[FormIds]")
{
    constexpr uint32_t cTamriel = 0x3C;
    CHECK(ToSessionCell(0xFF000A15, cTamriel) == GameId{});
    CHECK(ToSessionCell(0x00009732, cTamriel) == GameId(0, 0x9732));
    // An interior is only its cell: a made one has no id another game knows.
    CHECK_FALSE(ToSessionCell(0xFF000A15, 0).has_value());
    CHECK(ToSessionCell(0x000A2C8E, 0) == GameId(0, 0xA2C8E));
}
