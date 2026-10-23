// Engine names compare and key case-insensitively (Games/EngineNames.h, docs/re/systems/engine-names.md).
#include <catch2/catch.hpp>

#include <Games/EngineNames.h>

TEST_CASE("Engine names, any spelling", "[names]")
{
    CHECK(SameName("GetPlayer", "getplayer"));
    CHECK(SameName("", ""));
    CHECK(!SameName("GetPlayer", "GetPlayers"));
    CHECK(!SameName("GetPlayer", "GetPlayed"));
    CHECK(NameKey("RegisterForSingleUpdate") == "registerforsingleupdate");
}
