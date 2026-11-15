// Combat across games: a hit on another game's actor is a request its owner applies, with the kill credited to
// the attacker (ActorValueService); a projectile replays from the shooter's copies (CombatService).
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;
constexpr uint32_t kFirebolt = 0x0001C789;

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

SCENARIO("A host NPC's aim at a running guest's copy leads to where the guest will be", "[combat][copy-clock]")
{
    // Faendal's arrows (docs/re/systems/combat-aim.md): the host's NPC aims at the copy's predicted place (47309), the
    // arrow flies again on the guest's game, and hits there only if the guest is at that place when it arrives.
    constexpr uint32_t kDelayMs = 60;
    constexpr float kSpeed = 300.f;  // a running strafe
    constexpr float kFlight = 0.3f;  // 800 units at the hunting bow's arrow speed
    Party party(1, SmokeWorld, true, NetConditions{.DelayMs = kDelayMs, .Seed = 5});
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's copy");
    const uint32_t cCopy = PlayerCopy(host);

    float& running = guest.Actors.at(FakeEngine::kPlayer).Position[0];
    const float cFrom = running;
    const auto cStart = std::chrono::steady_clock::now();
    const auto cRun = [&](std::chrono::milliseconds aFor, const std::function<void()>& acEachFrame) {
        while (std::chrono::steady_clock::now() - cStart < aFor)
        {
            running = cFrom + kSpeed * std::chrono::duration<float>(std::chrono::steady_clock::now() - cStart).count();
            party.Frame();
            acEachFrame();
        }
    };
    cRun(std::chrono::milliseconds(1500), [] {}); // the copy's clock learns the line

    double off = 0.0;
    size_t samples = 0;
    cRun(std::chrono::milliseconds(3000), [&] {
        // Where the guest's game has the guest when the arrow, sent over the line, lands there.
        const float cThen = running + kSpeed * (kFlight + kDelayMs / 1000.f);
        off += std::abs(host.PredictedPosition(cCopy, kFlight)[0] - cThen);
        ++samples;
    });
    INFO("copy delay " << party.Host().GetCopyClock().Delay() << " ms, line " << party.Host().GetCopyClock().Network() << " ms");
    THEN("the aim lands within a body's width of the guest, on average")
        CHECK(off / samples < 20.0);
}

SCENARIO("A host NPC's arrow at a running guest meets the guest on the guest's game", "[combat][copy-clock]")
{
    // The whole of Faendal's shot (combat-aim.md): the host's NPC aims at the copy (44396 over 47309) and launches,
    // the launch goes over the line and the guest's game looses it again from the copy (CombatService), and there it
    // hits only if it passes the guest where the guest is when it arrives.
    constexpr uint32_t kDelayMs = 60;
    constexpr float kSpeed = 300.f;       // a running strafe
    constexpr float kArrowSpeed = 2655.f; // the hunting bow's iron arrows (the bow's aim, measured)
    Party party(1, SmokeWorld, true, NetConditions{.DelayMs = kDelayMs, .Seed = 9});
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t archer = 0;
    party.As(party.Host(), [&] { archer = host.LoadNpc(kGuard, kSmoke, {0.f, 800.f, 0.f}); });
    party.Until([&] { return PlayerCopy(host) != 0 && guest.CopiesOf(kGuard).size() == 1; }, "each game has the other's actors");
    const uint32_t cCopy = PlayerCopy(host);
    const uint32_t cArcherCopy = guest.CopiesOf(kGuard)[0];

    float& running = guest.Actors.at(FakeEngine::kPlayer).Position[0];
    const float cFrom = running;
    const auto cStart = std::chrono::steady_clock::now();
    const auto cGuestAt = [&](std::chrono::steady_clock::time_point aWhen) {
        const auto& cPlayer = guest.Actors.at(FakeEngine::kPlayer);
        return glm::vec3(cFrom + kSpeed * std::chrono::duration<float>(aWhen - cStart).count(), cPlayer.Position[1], cPlayer.Position[2]);
    };
    auto nextShot = cStart + std::chrono::milliseconds(1500); // the copy's clock learns the line first
    while (std::chrono::steady_clock::now() - cStart < std::chrono::milliseconds(4500))
    {
        running = cGuestAt(std::chrono::steady_clock::now()).x;
        party.Frame();
        if (std::chrono::steady_clock::now() >= nextShot && nextShot < cStart + std::chrono::milliseconds(4000))
        {
            party.As(party.Host(), [&] { host.ShootsAt(archer, cCopy, kArrowSpeed); });
            nextShot += std::chrono::milliseconds(250);
        }
    }

    double off = 0.0;
    size_t arrows = 0;
    for (const auto& cShot : guest.Shots)
    {
        if (cShot.Shooter != cArcherCopy)
            continue;
        // Where the guest's body point is when the arrow, loosed here at cShot.At, reaches its distance.
        auto arrival = cShot.At;
        glm::vec3 body{};
        for (int i = 0; i < 4; ++i)
        {
            body = cGuestAt(arrival) + glm::vec3(0.f, 0.f, FakeEngine::kFireNodeHeight);
            arrival = cShot.At + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                     std::chrono::duration<float>(glm::distance(body, cShot.Origin) / kArrowSpeed));
        }
        const glm::vec3 cPassed = cShot.Along(glm::distance(body, cShot.Origin));
        off += glm::distance(glm::vec2(cPassed), glm::vec2(body));
        ++arrows;
    }
    REQUIRE(arrows >= 8);
    INFO(arrows << " arrows, copy delay " << party.Host().GetCopyClock().Delay() << " ms");
    THEN("the arrows pass within a body's width of the guest, on average")
        CHECK(off / arrows < 20.0);
}
