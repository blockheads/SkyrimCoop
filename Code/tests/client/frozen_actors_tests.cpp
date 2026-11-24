#include <catch2/catch.hpp>

#include <TestHarness/FrozenActors.h>

#include <map>
#include <vector>

using TestHarness::FrozenActors;
using Life = FrozenActors::Life;

namespace
{
struct Polled
{
    std::vector<uint32_t> Thawed, Forgotten;
};

Polled Poll(FrozenActors& aFrozen, const std::map<uint32_t, Life>& acLives)
{
    Polled polled;
    aFrozen.Poll([&acLives](uint32_t aFormId) { return acLives.contains(aFormId) ? acLives.at(aFormId) : Life::kAlive; },
                 [&polled](uint32_t aFormId) { polled.Thawed.push_back(aFormId); },
                 [&polled](uint32_t aFormId) { polled.Forgotten.push_back(aFormId); });
    return polled;
}
} // namespace

// Skyrim (duo, 2026-11-23): a host NPC staged frozen and killed never finished dying (no graph update without
// kProcessMe); its ragdoll drifted keyframed into a hillside and the guest's copy lay elsewhere.
TEST_CASE("A frozen actor that dies is thawed once, the living stay frozen", "[frozen-actors]")
{
    FrozenActors frozen;
    frozen.Set(0x10, true);
    frozen.Set(0x20, true);
    CHECK(Poll(frozen, {}).Thawed.empty());

    const auto cPolled = Poll(frozen, {{0x20, Life::kDead}});
    CHECK(cPolled.Thawed == std::vector<uint32_t>{0x20});
    CHECK(cPolled.Forgotten.empty());
    CHECK_FALSE(frozen.Frozen(0x20));
    CHECK(frozen.Frozen(0x10));
    CHECK(Poll(frozen, {{0x20, Life::kDead}}).Thawed.empty()); // thawed once
}

TEST_CASE("A frozen actor that is gone is forgotten, not thawed, and not looked up again", "[frozen-actors]")
{
    FrozenActors frozen;
    frozen.Set(0x10, true);
    const auto cPolled = Poll(frozen, {{0x10, Life::kGone}});
    CHECK(cPolled.Thawed.empty());
    CHECK(cPolled.Forgotten == std::vector<uint32_t>{0x10});
    size_t looked = 0;
    frozen.Poll([&looked](uint32_t) { ++looked; return Life::kGone; }, [](uint32_t) {}, [](uint32_t) {});
    CHECK(looked == 0);
}

TEST_CASE("An actor thawed by hand is not thawed again at its death", "[frozen-actors]")
{
    FrozenActors frozen;
    frozen.Set(0x10, true);
    frozen.Set(0x10, false);
    CHECK(Poll(frozen, {{0x10, Life::kDead}}).Thawed.empty());
}
