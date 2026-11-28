#include <join/SaveList.h>
#include <catch2/catch.hpp>

using namespace SaveList;

TEST_CASE("The Journal lists every playthrough at the main menu and the loaded one in a game", "[save]")
{
    CHECK(For(true, false) == Limit::kAll);
    CHECK(For(false, false) == Limit::kCurrent);
    CHECK(For(false, true) == Limit::kNothing);
    CHECK(For(true, true) == Limit::kNothing);
}

TEST_CASE("In a game only the current character's saves and row are listed, never Show all", "[save]")
{
    CHECK(Shows(Limit::kAll, 0x0BADF00D, 0x0C0FFEE0));
    CHECK(Shows(Limit::kAll, 0x0BADF00D, kAllCharacters));
    CHECK(Shows(Limit::kCurrent, 0x0BADF00D, 0x0BADF00D));
    CHECK_FALSE(Shows(Limit::kCurrent, 0x0BADF00D, 0x0C0FFEE0));
    CHECK_FALSE(Shows(Limit::kCurrent, 0x0BADF00D, kAllCharacters));
    CHECK_FALSE(Shows(Limit::kNothing, 0x0BADF00D, 0x0BADF00D));
}
