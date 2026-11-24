// Combat across games: a hit on another game's actor is a request its owner applies, with the kill credited to
// the attacker (ActorValueService); a projectile replays from the shooter's copies (CombatService).
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kFirebolt = 0x0001C789;
constexpr uint32_t kStormBolt = 0x000E4CB7;  // StormCallLightningBolt01, cast by magicProjectileStormScript
constexpr uint32_t kStormActivator = 0xFF000900; // the script's PlaceAtMe'd activator in the sky

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

SCENARIO("A storm bolt a script casts from an activator shows on every game", "[combat]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0 && PlayerCopy(guest) != 0; }, "each game has the other's player");
    const auto cShows = [](const FakeEngine& acEngine, const glm::vec3& acOrigin) {
        return std::ranges::any_of(acEngine.Shots, [&](const auto& acShot) {
            return acShot.Shooter == 0 && acShot.Spell == kStormBolt && glm::distance(acShot.Origin, acOrigin) < 1.f;
        });
    };

    // Storm Call on an NPC: the host's game runs the effect's script.
    party.As(party.Host(), [&] { host.ObjectShoots(kStormActivator, kStormBolt, kSmoke, {10.f, 20.f, 3600.f}); });
    party.Until([&] { return cShows(guest, {10.f, 20.f, 3600.f}); },
                "the guest's game launches the host's bolt from the same place, with no shooter to apply it");
    CHECK(host.Shots.size() == 1);

    // Storm Call on the guest's own player: the guest's game runs it.
    party.As(party.Guest(), [&] { guest.ObjectShoots(kStormActivator + 1, kStormBolt, kSmoke, {-30.f, 5.f, 3600.f}); });
    party.Until([&] { return cShows(host, {-30.f, 5.f, 3600.f}); }, "the host's game launches the guest's bolt");
    CHECK(guest.Shots.size() == 2);

    // An object's shot with no spell (an arrow trap's) stays its game's: the bolt after it arrives alone.
    party.As(party.Host(), [&] {
        host.ObjectShoots(kStormActivator + 2, 0, kSmoke, {0.f, 0.f, 0.f});
        host.ObjectShoots(kStormActivator, kStormBolt, kSmoke, {50.f, 50.f, 3600.f});
    });
    party.Until([&] { return cShows(guest, {50.f, 50.f, 3600.f}); }, "the guest's game launches the next bolt");
    THEN("the arrow was not sent") CHECK(std::ranges::none_of(guest.Shots, [](const auto& acShot) { return acShot.Spell == 0; }));
}
