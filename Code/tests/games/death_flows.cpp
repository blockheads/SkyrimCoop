// Death and respawn across games (docs/design/copy-contract.md): a copy dies by its owner's death, never by its own
// health; a player goes down and gets up on its own game, and the others spawn its copy again.
#include "Party.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
constexpr glm::vec3 kSmokeMarker{500.f, 400.f, 0.f}; // QASmoke's COC marker

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.CocMarkers[kSmoke] = kSmokeMarker;
}

// aGame's copy of the other game's player, by form id (0: none).
uint32_t PlayerCopy(const FakeEngine& acEngine)
{
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player)
            return cId;
    return 0;
}
} // namespace

SCENARIO("A host NPC dies: its copy dies with it", "[death]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    uint32_t guard = 0;
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {20.f, 20.f, 0.f}); });
    party.Until([&] { return guest.CopiesOf(kGuard).size() == 1; }, "the guest has the host's guard");
    party.Agree("the games agree on the guard");

    WHEN("the host's player kills the guard")
    {
        party.As(party.Host(), [&] { host.Hit(guard, 200.f); });
        REQUIRE(host.IsActorDead(guard));
        const uint32_t cCopy = guest.CopiesOf(kGuard)[0];
        party.Until([&] { return guest.GetActorValue(cCopy, FakeEngine::kHealth) <= 0.f; },
                    "the copy shows the guard's health",
                    [&] {
                        return std::format("the guard's health {}, its copy's {}",
                                           host.GetActorValue(guard, FakeEngine::kHealth),
                                           guest.GetActorValue(cCopy, FakeEngine::kHealth));
                    });
        THEN("it is not dead by its own health (deferred kill), but by the guard's death")
        {
            CHECK(!guest.IsActorDead(cCopy));
            party.Until([&] { return guest.IsActorDead(cCopy); }, "the guest's copy dies");
            party.Agree("the games agree on the dead guard");
        }
    }
}

SCENARIO("A guest's player goes down and gets up: its copy on the host is spawned again", "[death]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Agree("the games agree on the players");
    const uint32_t cCopy = PlayerCopy(host);
    const uint32_t cMade = host.Actors.at(cCopy).Made;

    WHEN("the guest's player takes a deadly hit")
    {
        party.As(party.Guest(), [&] { guest.Hit(FakeEngine::kPlayer, 200.f); });
        THEN("it goes down, not dead: the session's death system makes it essential")
        {
            CHECK(guest.IsPlayerBleedingOut());
            CHECK(guest.IsPlayerAlive());
        }
        party.Until([&] { return host.GetActorValue(PlayerCopy(host), FakeEngine::kHealth) <= 0.f; },
                    "the host's copy shows its health");

        AND_THEN("it gets up at QASmoke's marker, and the host has a new copy of it there")
        {
            party.Until([&] { return !guest.IsPlayerBleedingOut(); }, "the guest's player gets up");
            CHECK(glm::vec3(guest.Actors.at(FakeEngine::kPlayer).Position[0],
                            guest.Actors.at(FakeEngine::kPlayer).Position[1], 0.f) == kSmokeMarker);
            party.Until(
                [&] {
                    const uint32_t cNow = PlayerCopy(host);
                    return cNow && (cNow != cCopy || host.Actors.at(cNow).Made != cMade);
                },
                "the host spawns its copy again");
            party.Agree("the games agree on the players");
            CHECK(host.GetActorValue(PlayerCopy(host), FakeEngine::kHealth) == 100.f);
        }
    }
}
