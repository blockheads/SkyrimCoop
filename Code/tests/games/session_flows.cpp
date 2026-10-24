// The session (HostService, HostPlayers, JoinService): the host removes a guest, which can join again; a guest that
// loses the host reconnects by itself, and says so when it can't.
#include "Party.h"

#include <Services/JoinService.h>

namespace
{
void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x00031D2E;
    aEngine.LoadCell(0x00031D2E);
}

// What that game's join panel shows (JoinService's status, else the transport's).
const JoinStatus& PanelOf(World& aGame)
{
    return aGame.ctx().at<JoinService>().GetStatus();
}
} // namespace

SCENARIO("The host kicks a guest: it leaves, its copy goes, and it can join again", "[session]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    party.Until([&] { return !host.CopiesOf().empty(); }, "the host's game has the guest's player");

    const uint32_t cGuestId = party.Guest().GetTransport().GetLocalPlayerId();
    party.As(party.Host(), [&] { party.Host().GetHostService().KickPlayer(cGuestId); });
    party.Until([&] { return !party.Guest().GetTransport().IsOnline() && host.CopiesOf().empty(); },
                "the guest is out and the host's copy of its player is gone");
    THEN("the guest's panel says the host removed it, and it does not try to reconnect")
    {
        CHECK(PanelOf(party.Guest()).Current == JoinStatus::State::kEnded);
        CHECK(PanelOf(party.Guest()).Reason == "kicked");
    }

    party.Join(party.Guest());
    party.Agree("it joined again: each game has the other's player");
}

SCENARIO("The host's session drops and comes back: the guest reconnects by itself", "[session]")
{
    Party party(1, Plugins);
    World& host = party.Host();
    party.Agree("each game has the other's player");
    const uint16_t cPort = host.GetHostService().GetNetwork()->GetPort();

    party.As(host, [&] { host.GetHostService().StopHosting(); });
    party.Until([&] { return PanelOf(party.Guest()).Current == JoinStatus::State::kReconnecting; },
                "the guest's game lost the host: its panel says it reconnects");
    party.As(host, [&] {
        REQUIRE(host.GetHostService().StartHosting(cPort));
        host.GetTransport().ConnectLocal(*host.GetHostService().GetNetwork());
    });
    party.Until([&] { return party.Guest().GetTransport().IsOnline(); }, "the guest is back in the session");
    party.Agree("each game has the other's player again");
    CHECK(PanelOf(party.Guest()).Current == JoinStatus::State::kJoined);
}

SCENARIO("The host's session drops for good: after its tries the guest's panel says the connection was lost", "[session]")
{
    Party party(1, Plugins);
    party.Agree("each game has the other's player");
    party.As(party.Host(), [&] { party.Host().GetHostService().StopHosting(); });
    party.Until([&] { return PanelOf(party.Guest()).Current == JoinStatus::State::kEnded; },
                "the guest's tries run out: its panel says the session ended",
                [&] { return std::format("its panel: {}", ToString(PanelOf(party.Guest()).Current)); });
    CHECK(PanelOf(party.Guest()).Reason == "lost_connection");
    CHECK_FALSE(party.Guest().GetTransport().IsConnected());
}
