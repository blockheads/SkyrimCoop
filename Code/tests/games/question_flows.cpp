// Questions about the player with no player's action behind them ask about the party (docs/design/party-model.md,
// rule 3; party-calls.md, "Questions"): a quest's timer measuring how far the player is from its marker answers for
// the nearest player.
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;


} // namespace

SCENARIO("A quest's timer asks how far the player is: the nearest player answers", "[questions]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("the players are present");
    party.As(party.Host(), [&] { host.PlayerStands(kSmoke, {2000.f, 0.f, 0.f}); });
    party.As(party.Guest(), [&] { guest.PlayerStands(kSmoke, {110.f, 0.f, 0.f}); });
    party.Agree("the guest's movement reaches its copy");
    party.Until([&] {
        const auto cCopies = host.CopiesOf();
        return !cCopies.empty() && std::abs(host.Actors.at(cCopies[0]).Position[0] - 110.f) < 1.f;
    }, "the player's copy reaches the distance fixture");
    uint32_t marker{};
    party.As(party.Host(), [&] { marker = host.LoadNpc(kGuard, kSmoke, {100.f, 0.f, 0.f}); });
    const LocalCall cAsks{.Class = "ObjectReference",
                          .Function = "GetDistance",
                          .Self = LocalForm{marker},
                          .Arguments = {LocalForm{FakeEngine::kPlayer}}};

    WHEN("no player's action is behind it")
    {
        party.As(party.Host(), [&] { host.RunScript(cAsks); });
        THEN("it is the guest's distance, the nearer") CHECK(std::get<float>(*host.LastAnswer) < 50.f);
    }
    WHEN("the host's player's own action asks it")
    {
        party.As(party.Host(), [&] { host.RunScript(cAsks, 0x14); });
        THEN("it is the host's own distance") CHECK(std::get<float>(*host.LastAnswer) > 1000.f);
    }
}

SCENARIO("A quest's timer asks whether a guard sees the player: yes if it sees any player", "[questions]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("the players are present");
    party.As(party.Host(), [&] { host.PlayerStands(kSmoke, {5000.f, 0.f, 0.f}); });
    party.As(party.Guest(), [&] { guest.PlayerStands(kSmoke, {300.f, 0.f, 0.f}); });
    party.Agree("the guest's movement reaches its copy");
    uint32_t guard{};
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {0.f, 0.f, 0.f}); });
    const LocalCall cSees{.Class = "Actor",
                          .Function = "HasLOS",
                          .Self = LocalForm{guard},
                          .Arguments = {LocalForm{FakeEngine::kPlayer}}};

    WHEN("no player's action is behind it")
    {
        party.As(party.Host(), [&] { host.RunScript(cSees); });
        THEN("the guest in sight makes it yes, though the host is out of it") CHECK(std::get<bool>(*host.LastAnswer));
    }
    WHEN("the host's player's own action asks it")
    {
        party.As(party.Host(), [&] { host.RunScript(cSees, 0x14); });
        THEN("only the host's own sight counts") CHECK(!std::get<bool>(*host.LastAnswer));
    }
}
