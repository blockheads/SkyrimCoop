// ArrivalStack: who passes through whom on the party's arrival spot (host-authority-migration.md, "Arriving together").
#include <catch2/catch.hpp>

#include <Services/ArrivalStack.h>

#include <algorithm>

namespace
{
using namespace std::chrono_literals;
constexpr uint32_t kPlayer = 0x14;
constexpr uint32_t kGuest = 0xFF000801;   // the other player's copy
constexpr uint32_t kFollower = 0xFF000900; // a follower's copy
constexpr uint32_t kBystander = 0x1A67C;  // an NPC elsewhere in the room
const glm::vec3 cSpot{100.f, 100.f, 0.f};
const glm::vec3 cAway{400.f, 100.f, 0.f};
const ArrivalStack::Clock::time_point cStart{1s};

bool Has(const std::vector<uint32_t>& acIds, uint32_t aId)
{
    return std::ranges::find(acIds, aId) != acIds.end();
}
} // namespace

TEST_CASE("A follower landing on the arrival spot passes through the players until it walks off", "[arrival-stack]")
{
    ArrivalStack stack(cSpot, cStart, {kPlayer, kGuest});
    const std::unordered_map<uint32_t, glm::vec3> cPlayers{{kPlayer, cSpot}, {kGuest, cSpot}};

    // A player still on its way passes through already; an actor elsewhere never joins.
    auto passing = stack.Update({{kPlayer, cSpot}}, {{kBystander, cAway}}, cStart);
    CHECK(Has(passing, kGuest));
    CHECK_FALSE(Has(passing, kBystander));

    passing = stack.Update(cPlayers, {{kFollower, cSpot + glm::vec3(20.f, 0.f, 0.f)}, {kBystander, cAway}}, cStart + 1s);
    CHECK(Has(passing, kFollower));
    CHECK(Has(passing, kPlayer));
    CHECK_FALSE(Has(passing, kBystander));

    // Off the spot it collides at once, and leaves after half a second off it.
    passing = stack.Update(cPlayers, {{kFollower, cAway}}, cStart + 2s);
    CHECK_FALSE(Has(passing, kFollower));
    stack.Update(cPlayers, {{kFollower, cAway}}, cStart + 2s + ArrivalStack::kWalkOff);
    // Back on the spot it is no member again: it walked off.
    passing = stack.Update(cPlayers, {{kFollower, cSpot}}, cStart + 3s);
    CHECK_FALSE(Has(passing, kFollower));
}

TEST_CASE("After the arrival nobody new joins the stack", "[arrival-stack]")
{
    ArrivalStack stack(cSpot, cStart, {kPlayer});
    const auto passing = stack.Update({{kPlayer, cSpot}}, {{kFollower, cSpot}}, cStart + ArrivalStack::kArrival + 1s);
    CHECK_FALSE(Has(passing, kFollower));
}

TEST_CASE("An actor on the stack that is gone leaves it; a player away is still coming", "[arrival-stack]")
{
    ArrivalStack stack(cSpot, cStart, {kPlayer, kGuest});
    stack.Update({{kPlayer, cSpot}, {kGuest, cSpot}}, {{kFollower, cSpot}}, cStart);
    const auto passing = stack.Update({{kPlayer, cSpot}}, {}, cStart + 1s);
    CHECK_FALSE(Has(passing, kFollower));
    CHECK(Has(passing, kGuest));
    // A player who never came within the arrival leaves it.
    ArrivalStack late(cSpot, cStart, {kPlayer, kGuest});
    CHECK_FALSE(Has(late.Update({{kPlayer, cSpot}}, {}, cStart + ArrivalStack::kArrival + 1s), kGuest));
}
