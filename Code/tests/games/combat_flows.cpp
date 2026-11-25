// Combat across games: a hit on another game's actor is a request its owner applies, with the kill credited to
// the attacker (ActorValueService); a projectile replays from the shooter's copies (CombatService).
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kFirebolt = 0x0001C789;
constexpr uint32_t kFollower = 0x000A2C8E; // Lydia

} // namespace

SCENARIO("A guest's sword on a host NPC: the host's game applies it, and the kill is the guest's", "[combat]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && PlayerCopy(host) != 0; },
                "each game has the other's actors");
    party.Agree("the games agree on the guard");
    const auto cCopy = [&] { return guest.CopiesOf(kGuard)[0]; };
    const float cFull = host.GetActorValue(guard, FakeEngine::kHealth);

    party.As(party.Guest(), [&] { guest.Strikes(FakeEngine::kPlayer, cCopy(), 30.f); });
    party.Until([&] { return host.GetActorValue(guard, FakeEngine::kHealth) == cFull - 30.f; },
                "the host's guard takes the hit");
    THEN("the guest's copy took nothing itself, and the guest sees the guard's health bar")
    {
        CHECK(guest.GetActorValue(cCopy(), FakeEngine::kHealth) <= cFull);
        CHECK(std::ranges::count(guest.HealthBarsShown, cCopy()) >= 1);
    }

    party.As(party.Guest(), [&] { guest.Strikes(FakeEngine::kPlayer, cCopy(), cFull); });
    party.Until([&] { return host.IsActorDead(guard); }, "the host's guard dies of the guest's blow");
    CHECK(host.Actors.at(guard).KilledBy == PlayerCopy(host));
    party.Agree("the guest's copy of the guard dies with it");
}

SCENARIO("A guest's fire bolt flies on the host's game", "[combat]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    party.As(party.Guest(), [&] { guest.Shoots(FakeEngine::kPlayer, kFirebolt, {10.f, 20.f, 120.f}); });
    party.Until(
        [&] {
            return std::ranges::any_of(host.Shots, [&](const auto& acShot) {
                return acShot.Shooter == PlayerCopy(host) && acShot.Spell == kFirebolt &&
                       glm::distance(acShot.Origin, glm::vec3(10.f, 20.f, 120.f)) < 1.f;
            });
        },
        "the host's copy of the guest launches the same bolt from the same place");
    THEN("the guest's own bolt is not sent back to it") CHECK(guest.Shots.size() == 1);
}

SCENARIO("A host NPC whose combat group targets a player is an enemy on that player's compass", "[combat][compass]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t wolf = 0, follower = 0;
    party.As(party.Host(), [&] {
        wolf = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f});
        follower = host.LoadNpc(kFollower, kSmoke, {60.f, 40.f, 0.f});
    });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && PlayerCopy(host) != 0; },
                "each game has the other's actors");
    const auto cCopy = [&] { return guest.CopiesOf(kGuard)[0]; };

    // Skyrim (compass-enemies-on-guest): the wolf bit the follower while its group targeted the host.
    party.As(party.Host(), [&] {
        host.Fights(wolf, follower);
        host.GroupTargets(wolf, {follower, FakeEngine::kPlayer});
    });
    party.Until([&] { return host.CompassEnemies() == std::vector{wolf}; }, "the host's compass shows the wolf");
    for (int i = 0; i < 30; ++i)
        party.Frame();
    CHECK(guest.CompassEnemies().empty()); // its group has not seen the guest

    party.As(party.Host(), [&] { host.GroupTargets(wolf, {follower, FakeEngine::kPlayer, PlayerCopy(host)}); });
    party.Until([&] { return guest.CompassEnemies() == std::vector{cCopy()}; },
                "the guest's compass shows the wolf once its group targets the guest");

    // 41240 asks no combat state: a member whose own state reads 0 stays while its group targets the player.
    party.As(party.Host(), [&] {
        host.Fights(wolf, 0);
        host.GroupTargets(wolf, {FakeEngine::kPlayer, PlayerCopy(host)});
    });
    CHECK(host.GetActorAiFields(wolf).CombatState == 0);
    party.Until([&] { return host.CompassEnemies() == std::vector{wolf} && guest.CompassEnemies() == std::vector{cCopy()}; },
                "both compasses keep the wolf its group still sets on the players");

    party.As(party.Host(), [&] { host.GroupTargets(wolf, {}); });
    party.Until([&] { return host.CompassEnemies().empty() && guest.CompassEnemies().empty(); },
                "the fight ends on both compasses");
}

SCENARIO("Copies of one host combat group hold one group on the guest's game", "[combat][copy-combat]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t first = 0, second = 0;
    party.As(party.Host(), [&] {
        first = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f});
        second = host.LoadNpc(kGuard, kSmoke, {60.f, 40.f, 0.f});
    });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 2 && PlayerCopy(host) != 0; }, "each game has the other's actors");

    party.As(party.Host(), [&] {
        host.Actors.at(first).GroupId = host.Actors.at(second).GroupId = 7;
        host.Fights(first, FakeEngine::kPlayer);
        host.Fights(second, PlayerCopy(host));
    });
    const auto cKeys = [&] {
        std::set<uint64_t> keys;
        for (const uint32_t cCopy : guest.CopiesOf(kGuard))
            keys.insert(guest.Actors.at(cCopy).CopyGroupKey);
        return keys;
    };
    party.Until([&] { return cKeys().size() == 1 && *cKeys().begin() != 0; }, "both copies hold the host's one group");
    // Each copy fights its own owner's target in it.
    THEN("the guest's player is in combat") CHECK(guest.PlayerFlaggedInCombat());

    party.As(party.Host(), [&] {
        host.Fights(first, 0);
        host.Fights(second, 0);
    });
    party.Until([&] { return cKeys() == std::set<uint64_t>{0} && !guest.PlayerFlaggedInCombat(); },
                "the fight ends: the copies leave the group");
}

SCENARIO("A player's copy is in combat while its player's own group holds enemies", "[combat][copy-combat]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && PlayerCopy(guest) != 0; }, "each game has the other's actors");

    // Skyrim (copy-regen-rate-in-combat): the host's player's copy reads in combat, as the regen rate asks (38460).
    party.As(party.Host(), [&] { host.Fights(FakeEngine::kPlayer, guard); });
    party.Until([&] { return guest.GetActorAiFields(PlayerCopy(guest)).CombatState == 1; }, "the host's player's copy is in combat");
    CHECK(guest.Actors.at(PlayerCopy(guest)).GroupTargets == std::vector{guest.CopiesOf(kGuard)[0]});
    CHECK_FALSE(guest.PlayerFlaggedInCombat()); // that group targets the guard, not the guest

    party.As(party.Host(), [&] { host.Fights(FakeEngine::kPlayer, 0); });
    party.Until([&] { return guest.GetActorAiFields(PlayerCopy(guest)).CombatState == 0; }, "out of combat with its player");
}
