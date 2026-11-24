#include <catch2/catch.hpp>

#include <TestHarness/FrozenActors.h>

using TestHarness::FrozenActors;

// Skyrim (duo, 2026-11-23): a host NPC staged frozen and killed never finished dying (no graph update without
// kProcessMe); its ragdoll drifted keyframed into a hillside and the guest's copy lay elsewhere.
TEST_CASE("A frozen actor that dies is thawed once, the living stay frozen", "[frozen-actors]")
{
    FrozenActors frozen;
    frozen.Set(0x10, true);
    frozen.Set(0x20, true);
    std::set<uint32_t> dead;
    const auto cDead = [&dead](uint32_t aFormId) { return dead.contains(aFormId); };

    CHECK(frozen.TakeDead(cDead).empty());
    dead.insert(0x20);
    CHECK(frozen.TakeDead(cDead) == std::vector<uint32_t>{0x20});
    CHECK_FALSE(frozen.Frozen(0x20));
    CHECK(frozen.Frozen(0x10));
    CHECK(frozen.TakeDead(cDead).empty()); // thawed once
}

TEST_CASE("An actor thawed by hand is not thawed again at its death", "[frozen-actors]")
{
    FrozenActors frozen;
    frozen.Set(0x10, true);
    frozen.Set(0x10, false);
    CHECK(frozen.TakeDead([](uint32_t) { return true; }).empty());
}
