// Engine names compare and key case-insensitively (sync/core/EngineNames.h, docs/re/systems/engine-names.md).
#include <catch2/catch.hpp>

#include <DisplayNames.h>
#include <core/EngineNames.h>

TEST_CASE("Engine names, any spelling", "[names]")
{
    CHECK(SameName("GetPlayer", "getplayer"));
    CHECK(SameName("", ""));
    CHECK(!SameName("GetPlayer", "GetPlayers"));
    CHECK(!SameName("GetPlayer", "GetPlayed"));
    CHECK(NameKey("RegisterForSingleUpdate") == "registerforsingleupdate");
}

TEST_CASE("A quest's name cache clear keeps a custom name", "[names]")
{
    // ExtraTextDisplayData: a custom name (no message, no quest, instance -2) is only its cache (12769, names.md).
    CHECK_FALSE(DisplayNames::CacheClears(false, false, DisplayNames::kCustomInstance));
    CHECK(DisplayNames::CacheClears(true, true, 1)); // an alias's message name regenerates
    CHECK(DisplayNames::CacheClears(false, false, -1)); // none set yet
}
