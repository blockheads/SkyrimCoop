#include "Party.h"
#include "Fixtures.h"

#include <Events/MenuEvent.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Services/SessionService.h>

namespace
{

} // namespace

SCENARIO("A guest quitting to the main menu leaves the session", "[session-exit]")
{
    Party party(1, SmokeWorld);
    party.Agree("the players are present");
    party.As(party.Guest(), [&] {
        Party::Engine(party.Guest()).AtMainMenu = true;
        party.Guest().GetDispatcher().trigger(MenuEvent{"Main Menu", true});
    });
    party.Until(
        [&] { return !party.Guest().GetTransport().IsConnected() && Party::Engine(party.Host()).CopiesOf().empty(); },
        "the guest leaves and its copy goes away");
    CHECK(party.Host().GetHostService().IsHosting());
}

SCENARIO("The host quitting to the main menu ends the session for its guest", "[session-exit]")
{
    Party party(1, SmokeWorld);
    party.Agree("the players are present");
    party.As(party.Host(), [&] {
        Party::Engine(party.Host()).AtMainMenu = true;
        party.Host().GetDispatcher().trigger(MenuEvent{"Main Menu", true});
    });
    party.Until([&] { return !party.Host().GetHostService().IsHosting() && Party::Engine(party.Guest()).AtMainMenu; },
                "hosting ends and the guest returns to the main menu");
    CHECK_FALSE(party.Guest().GetTransport().IsConnected());
    CHECK(party.Guest().ctx().at<JoinService>().GetStatus().Reason == "host_left");
}

SCENARIO("A kicked guest returns to the main menu with its reason", "[session-exit]")
{
    Party party(1, SmokeWorld);
    party.As(party.Host(),
             [&] { party.Host().GetHostService().KickPlayer(party.Guest().GetTransport().GetLocalPlayerId()); });
    party.Until([&] { return Party::Engine(party.Guest()).AtMainMenu; }, "the kicked guest returns to the main menu");
    CHECK(party.Guest().ctx().at<JoinService>().GetStatus().Reason == "kicked");
    CHECK_FALSE(party.Guest().ctx().at<JoinService>().IsJoining());
}

SCENARIO("A guest leaving the session returns to the main menu and can join again", "[session-exit]")
{
    Party party(1, SmokeWorld);
    party.As(party.Guest(), [&] { party.Guest().GetSessionService().End(); });
    party.Until([&] { return Party::Engine(party.Guest()).AtMainMenu; }, "leaving returns to the main menu");
    CHECK(party.Host().GetHostService().IsHosting());
    CHECK_FALSE(party.Guest().GetTransport().IsConnected());
    party.Join(party.Guest());
    party.Until([&] { return party.Guest().GetTransport().IsOnline(); }, "the guest joins again");
    CHECK_FALSE(party.Guest().GetSessionService().IsEnding());
}

SCENARIO("A guest quitting keeps the character already received by the host", "[session-exit]")
{
    Party party(1, SmokeWorld);
    auto& game = party.Guest();
    const std::string cKey = game.ctx().at<JoinService>().PlayerKey();
    party.As(game, [&] {
        Party::Engine(game).Player.Level = 19;
        game.GetDispatcher().trigger(MenuEvent{"InventoryMenu", false});
    });
    party.Until(
        [&] {
            const auto* pRecord = party.Host().ctx().at<SaveService>().GetRecords().Find(cKey);
            return pRecord && pRecord->Level == 19;
        },
        "the host receives the guest's character");
    party.As(game, [&] { game.GetSessionService().QuitToMainMenu(); });
    CHECK(party.Host().ctx().at<SaveService>().GetRecords().Find(cKey)->Level == 19);
    CHECK_FALSE(game.GetTransport().IsConnected());
}
