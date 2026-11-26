// Actor::SetCriticalStage's rule (docs/re/systems/death-disintegrate.md): this game's player is never turned to ash or
// goo, as the engine would leave it with no 3D and its frame waiting on the player's update.
#include <catch2/catch.hpp>

#include <CriticalStageRules.h>

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

TEST_CASE("A refused ash or goo end hides this game's dead player's body", "[CriticalStage]")
{
    CHECK(HidesBody(true, true, kDisintegrateEnd));
    CHECK(HidesBody(true, true, kGooEnd));
    CHECK_FALSE(HidesBody(true, true, kDisintegrateStart));
    CHECK_FALSE(HidesBody(false, true, kDisintegrateEnd));
    // The ash script's end after the revive: the living player stays shown.
    CHECK_FALSE(HidesBody(true, false, kDisintegrateEnd));
}

TEST_CASE("A hidden body is kept hidden only while its player is dead", "[CriticalStage]")
{
    CHECK(KeepsBodyHidden(true, true));
    // Revived, or a wipe's load: the mark goes, the camera has the roots again.
    CHECK_FALSE(KeepsBodyHidden(true, false));
    CHECK_FALSE(KeepsBodyHidden(false, true));
}
