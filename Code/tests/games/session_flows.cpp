// The session (HostService, HostPlayers): the host removes a guest, which can join again.
#include "Party.h"

namespace
{
void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x00031D2E;
    aEngine.LoadCell(0x00031D2E);
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

    party.Join(party.Guest());
    party.Agree("it joined again: each game has the other's player");
}
