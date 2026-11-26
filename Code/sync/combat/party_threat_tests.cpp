#include <combat/PartyThreat.h>
#include <array>
#include <catch2/catch.hpp>
#include <limits>

namespace
{
PartyThreat::State Fight(uint32_t aCurrent = 14)
{
    PartyThreat::State state;
    state.Active = true;
    state.Now = 2;
    state.Players = {14, 99};
    state.Combats[500] = {aCurrent, 0};
    return state;
}
} // namespace

TEST_CASE("Party threat favors greater recent damage without a host bonus", "[party-aggro]")
{
    for (const auto cCurrent : {14u, 99u})
    {
        auto state = Fight(cCurrent);
        const uint32_t cOther = cCurrent == 14 ? 99 : 14;
        state.Hit(500, cCurrent, 5);
        state.Hit(500, cOther, 30);
        const std::array targets{PartyThreat::Candidate{14, 250, true}, PartyThreat::Candidate{99, 250, true}};
        CHECK(state.Choose(500, cCurrent, targets) == cOther);
    }
}

TEST_CASE("Party threat avoids ties, small score changes and rapid target flipping", "[party-aggro]")
{
    auto state = Fight();
    const std::array targets{PartyThreat::Candidate{14, 250, true}, PartyThreat::Candidate{99, 250, true}};
    state.Hit(500, 14, 10);
    state.Hit(500, 99, 10);
    CHECK(state.Choose(500, 14, targets) == 14);
    state.Hit(500, 99, 1);
    CHECK(state.Choose(500, 14, targets) == 14);
    state.Hit(500, 99, 10);
    CHECK(state.Choose(500, 14, targets) == 99);
    state.Combats[500] = {99, state.Now};
    state.Now += 0.2;
    state.Hit(500, 14, 100);
    CHECK(state.Choose(500, 99, targets) == 99);
    state.Now += 2;
    CHECK(state.Choose(500, 99, targets) == 14);
}

TEST_CASE("An invalid current target does not keep a living player waiting through the hold", "[party-aggro]")
{
    auto state = Fight();
    state.Combats[500].ChangedAt = state.Now;
    const std::array targets{PartyThreat::Candidate{14, 250, false}, PartyThreat::Candidate{99, 250, true}};
    CHECK(state.Choose(500, 14, targets) == 99);
    CHECK(state.Choose(500, 14, {}) == 14);
    state.Combats[500].Target = 99;
    CHECK(state.Choose(500, 14, targets) == 14);
}

TEST_CASE("Damage threat decays and rejected damage generates no threat", "[party-aggro]")
{
    auto state = Fight();
    state.Now = 0;
    state.Hit(500, 14, 10);
    CHECK(state.DamageOf(500, 14) == Approx(10));
    CHECK(state.RecentlyHit(500, 14));
    state.Now = 8;
    CHECK(state.DamageOf(500, 14) == Approx(5));
    CHECK_FALSE(state.RecentlyHit(500, 14));
    state.Hit(500, 14, 5);
    CHECK(state.DamageOf(500, 14) == Approx(10));
    state.Hit(500, 99, 0);
    state.Hit(500, 99, -20);
    state.Hit(500, 99, std::numeric_limits<float>::quiet_NaN());
    state.Hit(500, 99, std::numeric_limits<float>::infinity());
    CHECK(state.DamageOf(500, 99) == 0);
    state.Hit(500, 123, 100);
    state.Hit(14, 99, 100);
    CHECK(state.DamageOf(500, 123) == 0);
    CHECK(state.DamageOf(14, 99) == 0);
}

TEST_CASE("Threat includes proximity but preserves native eligibility and non-party fights", "[party-aggro]")
{
    auto state = Fight();
    state.Hit(500, 14, 1);
    state.Hit(500, 99, 1);
    std::array targets{PartyThreat::Candidate{14, 1500, true}, PartyThreat::Candidate{99, 100, true}};
    CHECK(state.Choose(500, 14, targets) == 99);
    targets[1].Eligible = false;
    state.Hit(500, 99, 1000);
    CHECK(state.Choose(500, 14, targets) == 14);
    targets[1].Eligible = true;
    targets[1].Distance = std::numeric_limits<float>::quiet_NaN();
    CHECK(state.Choose(500, 14, targets) == 14);
    targets[1].Distance = -1;
    CHECK(state.Choose(500, 14, targets) == 14);
    targets[1].Distance = 100;
    CHECK(state.Choose(500, 123, targets) == 123);
    CHECK(state.Choose(500, 0, targets) == 0);
    CHECK(state.Choose(501, 14, targets) == 14);
    state.Active = false;
    CHECK(state.Choose(500, 14, targets) == 14);
}

TEST_CASE("Native reevaluation respects the current target's hold and switching margin", "[party-aggro]")
{
    auto state = Fight(99);
    state.Hit(500, 14, 5);
    state.Hit(500, 99, 30);
    const std::array targets{PartyThreat::Candidate{14, 250, true}, PartyThreat::Candidate{99, 250, true}};
    CHECK(state.Resolve(500, 99, 14, targets) == 99);
    state.Combats[500].ChangedAt = state.Now;
    state.Hit(500, 14, 100);
    CHECK(state.Resolve(500, 99, 14, targets) == 99);
    state.Now += 2;
    CHECK(state.Resolve(500, 99, 14, targets) == 14);
    CHECK(state.Resolve(500, 99, 123, targets) == 123);
    CHECK(state.Resolve(500, 99, 0, targets) == 0);
    CHECK(state.Resolve(500, 14, 14, targets) == 14);
    state.Active = false;
    CHECK(state.Resolve(500, 99, 14, targets) == 14);
}

TEST_CASE("Recent visible damage recovers a lost attacker without revealing hidden players", "[party-aggro]")
{
    auto state = Fight();
    PartyThreat::Perception p{.Known = false, .Lost = true, .Detection = 0, .ClearPath = true};
    CHECK_FALSE(state.CanTarget(500, 99, 14, p));
    state.Hit(500, 99, 30);
    CHECK(state.CanTarget(500, 99, 14, p));
    p.ClearPath = false;
    CHECK_FALSE(state.CanTarget(500, 99, 14, p));
    p.ClearPath = true;
    p.Invisible = true;
    CHECK_FALSE(state.CanTarget(500, 99, 14, p));
    p.Invisible = false;
    p.Sneaking = true;
    CHECK_FALSE(state.CanTarget(500, 99, 14, p));
    p.Sneaking = false;
    state.Now += 4;
    CHECK_FALSE(state.CanTarget(500, 99, 14, p));
    CHECK(state.CanTarget(500, 99, 99, p));
    p.ClearPath = false;
    p.Known = true;
    p.Lost = false;
    CHECK(state.CanTarget(500, 99, 99, p));
    CHECK_FALSE(state.CanTarget(500, 99, 14, p));
}

TEST_CASE("An eligible live target survives selection before the next host snapshot", "[party-aggro]")
{
    auto state = Fight(14);
    state.Hit(500, 14, 5);
    state.Hit(500, 99, 30);
    std::array targets{PartyThreat::Candidate{14, 250, true}, PartyThreat::Candidate{99, 250, true}};
    CHECK(state.Resolve(500, 99, 14, targets) == 99);
    targets[1].Eligible = false;
    CHECK(state.Resolve(500, 99, 14, targets) == 14);
    targets[1].Eligible = true;
    CHECK(state.Resolve(500, 99, 123, targets) == 123);
}

TEST_CASE("In a party fight a seen player is fair game without a hit", "[party-aggro]")
{
    auto state = Fight(14);
    PartyThreat::Perception seen{.Known = true, .Lost = false, .Detection = 100, .Stealth = 40};
    CHECK_FALSE(state.CanTarget(500, 99, 14, seen)); // an ordinary fight: the guest must have hit it
    state.PartyFights.insert(500);
    CHECK(state.CanTarget(500, 99, 14, seen));
    seen.Lost = true;
    CHECK_FALSE(state.CanTarget(500, 99, 14, seen)); // still only one it sees
}

TEST_CASE("An NPC fighting one player is every player's foe, and no one else's", "[party-aggro]")
{
    auto state = Fight(99);
    CHECK_FALSE(state.IsPartyFoe(500, 14)); // not yet a party fight
    state.PartyFights.insert(500);
    CHECK(state.IsPartyFoe(500, 14)); // the host, though it fights the guest's copy
    CHECK(state.IsPartyFoe(500, 99));
    CHECK_FALSE(state.IsPartyFoe(500, 600)); // an NPC beside them keeps its reaction
    CHECK_FALSE(state.IsPartyFoe(501, 14));
    state.Active = false;
    CHECK_FALSE(state.IsPartyFoe(500, 14));
}

TEST_CASE("A guest's friendly-hit anger is the whole party's", "[party-aggro]")
{
    const uint64_t cAngry = uint64_t(500) << 32 | 0xFF000833; // 500 angry with the guest's copy
    CHECK(PartyThreat::AngerCovers(cAngry, 500, true));        // any player's body: the copy, the host's player
    CHECK_FALSE(PartyThreat::AngerCovers(cAngry, 500, false)); // an NPC beside them keeps its reaction
    CHECK_FALSE(PartyThreat::AngerCovers(cAngry, 501, true));  // another actor is not angry
    CHECK_FALSE(PartyThreat::AngerCovers(0, 0, true));         // an empty slot
}

TEST_CASE("The anger ends after a fight with any player", "[party-aggro]")
{
    CHECK_FALSE(PartyThreat::AngerFought(false, false)); // not yet fought: it stays angry out of combat
    CHECK(PartyThreat::AngerFought(false, true));        // fought the host, not the guest who hit it
    CHECK(PartyThreat::AngerFought(true, false));
}
