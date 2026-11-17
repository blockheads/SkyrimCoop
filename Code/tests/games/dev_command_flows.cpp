// The dev chat commands (docs/design/dev-commands.md): a world command runs in the host's game at the typer's
// character, a player command on every game's own player.
#include "FakeDevTools.h"
#include "Fixtures.h"
#include "Party.h"

#include <Services/DevToolsService.h>

namespace
{
constexpr uint32_t kWolves = 0xB83C2; // LCharWolf

FakeDevTools& Tools(World& aGame)
{
    return aGame.ctx().at<FakeDevTools&>();
}
} // namespace

SCENARIO("A guest's /heal heals every player, each on its own game", "[dev-commands]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");
    for (FakeEngine* pEngine : {&host, &guest})
        pEngine->Actors.at(FakeEngine::kPlayer).Values.ActorValuesList[FakeEngine::kHealth] = 20.f;

    party.As(party.Guest(), [&] { party.Guest().ctx().at<DevToolsService>().Run("/heal"); });
    party.Until(
        [&] {
            return host.GetActorValue(FakeEngine::kPlayer, FakeEngine::kHealth) == 100.f &&
                   guest.GetActorValue(FakeEngine::kPlayer, FakeEngine::kHealth) == 100.f;
        },
        "both players are back to full health");
    CHECK(std::ranges::count(Tools(party.Host()).Lines, "player.restoreav health 100000") == 1);
    CHECK(std::ranges::count(Tools(party.Guest()).Lines, "player.restoreav health 100000") == 1);
}

SCENARIO("A guest's /spawn places the wolves at its character in the host's game; the guest sees copies",
         "[dev-commands]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    party.As(party.Guest(), [&] { party.Guest().ctx().at<DevToolsService>().Run("/spawn wolf 2"); });
    party.Until([&] { return guest.CopiesOf(kWolves).size() == 2; }, "the guest sees the host's two wolves");
    CHECK(Tools(party.Guest()).Lines.empty());
    CHECK(Tools(party.Host()).Lines ==
          std::vector<std::string>{std::format("{:08X}.placeatme B83C2 2", PlayerCopy(host))});
    party.Agree("the games agree on the wolves");

    WHEN("the host types /killall")
    {
        party.As(party.Host(), [&] { party.Host().ctx().at<DevToolsService>().Run("/killall"); });
        party.Until(
            [&] {
                return std::ranges::all_of(guest.CopiesOf(kWolves),
                                           [&](uint32_t aCopy) { return guest.IsActorDead(aCopy); });
            },
            "the wolves die in the host's game, and their copies with them");
        CHECK_FALSE(host.IsActorDead(PlayerCopy(host)));
        CHECK_FALSE(host.IsActorDead(FakeEngine::kPlayer));
    }
}

SCENARIO("/killmove and /reset reach every game's kill move odds", "[dev-commands][kill-move]")
{
    Party party(1, SmokeWorld);
    party.As(party.Guest(), [&] { party.Guest().ctx().at<DevToolsService>().Run("/killmove 100"); });
    party.Until(
        [&] {
            return Tools(party.Host()).KillMoveOdds == std::optional<uint8_t>(100) &&
                   Tools(party.Guest()).KillMoveOdds == std::optional<uint8_t>(100);
        },
        "both games' attackers kill-move every fatal blow");
    party.As(party.Host(), [&] { party.Host().ctx().at<DevToolsService>().Run("/reset"); });
    party.Until([&] { return !Tools(party.Host()).KillMoveOdds && !Tools(party.Guest()).KillMoveOdds; },
                "both games are back to the game's own odds");
}

SCENARIO("/god follows each game's own god mode: on stays on, a toggle turns it off, reset clears it", "[dev-commands]")
{
    Party party(1, SmokeWorld);
    // The host's player is in god mode already (the console's tgm, as profile --load sets it).
    party.As(party.Host(), [&] { Tools(party.Host()).God = true; });

    party.As(party.Guest(), [&] { party.Guest().ctx().at<DevToolsService>().Run("/god on"); });
    party.Until([&] { return Tools(party.Guest()).God; }, "the guest's player is in god mode");
    CHECK(Tools(party.Host()).God);

    party.As(party.Guest(), [&] { party.Guest().ctx().at<DevToolsService>().Run("/god"); });
    party.Until([&] { return !Tools(party.Host()).God && !Tools(party.Guest()).God; }, "the toggle turns both off");

    party.As(party.Host(), [&] { Tools(party.Host()).God = true; });
    party.As(party.Host(), [&] { party.Host().ctx().at<DevToolsService>().Run("/reset"); });
    party.Until([&] { return !Tools(party.Host()).God; }, "reset turns the host's god mode off");
}
