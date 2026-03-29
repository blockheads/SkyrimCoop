#include <catch2/catch.hpp>
#include "dirty_cache.h"

TEST_CASE("First position check is dirty", "[DirtyCache]")
{
    DirtyCache cache;
    REQUIRE(cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("Unchanged position is clean", "[DirtyCache]")
{
    DirtyCache cache;
    cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE_FALSE(cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("Position moved beyond epsilon", "[DirtyCache]")
{
    DirtyCache cache;
    cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(cache.CheckPosition(0x14, 102.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("Rotation changed beyond epsilon", "[DirtyCache]")
{
    DirtyCache cache;
    cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.05f, 0.0f, 0.0f));
}

TEST_CASE("Remove makes next check dirty", "[DirtyCache]")
{
    DirtyCache cache;
    cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE_FALSE(cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f));

    cache.Remove(0x14);
    REQUIRE(cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("Clear resets all entries", "[DirtyCache]")
{
    DirtyCache cache;
    cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f);
    cache.CheckPosition(0x15, 400.0f, 500.0f, 600.0f, 0.0f, 0.0f, 0.0f);

    cache.Clear();

    REQUIRE(cache.CheckPosition(0x14, 100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 0.0f));
    REQUIRE(cache.CheckPosition(0x15, 400.0f, 500.0f, 600.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("ActorProcess dirty check", "[DirtyCache]")
{
    DirtyCache cache;

    // First call is always dirty
    REQUIRE(cache.CheckActorProcess(0x14, 0.016f));

    // Same delta is clean
    REQUIRE_FALSE(cache.CheckActorProcess(0x14, 0.016f));

    // Different delta is dirty
    REQUIRE(cache.CheckActorProcess(0x14, 0.033f));
}
