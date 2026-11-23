// Actor::SetCriticalStage's rule (docs/re/systems/death-disintegrate.md): this game's player is never turned to ash or
// goo, as the engine would leave it with no 3D and its frame waiting on the player's update.
#include <catch2/catch.hpp>

#include <Games/Skyrim/CriticalStageRules.h>

using namespace CriticalStage;

TEST_CASE("This game's player refuses every critical stage but none", "[CriticalStage]")
{
    for (const uint32_t cStage : {kGooStart, kGooEnd, kDisintegrateStart, kDisintegrateEnd})
        CHECK_FALSE(Takes(true, cStage));
    CHECK(Takes(true, kNone));
}

TEST_CASE("Any other actor takes its critical stages", "[CriticalStage]")
{
    for (const uint32_t cStage : {kNone, kGooStart, kGooEnd, kDisintegrateStart, kDisintegrateEnd})
        CHECK(Takes(false, cStage));
    CHECK(RemovesBody(kDisintegrateEnd));
    CHECK(RemovesBody(kGooEnd));
    CHECK_FALSE(RemovesBody(kDisintegrateStart));
}

TEST_CASE("Only this game's player leaves no ash pile", "[CriticalStage]")
{
    CHECK_FALSE(TakesAshPile(true));
    CHECK(TakesAshPile(false));
}

TEST_CASE("A refused ash or goo end hides this game's player's body", "[CriticalStage]")
{
    CHECK(HidesBody(true, kDisintegrateEnd));
    CHECK(HidesBody(true, kGooEnd));
    CHECK_FALSE(HidesBody(true, kDisintegrateStart));
    CHECK_FALSE(HidesBody(false, kDisintegrateEnd));
}
