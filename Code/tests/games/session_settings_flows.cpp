#include "Party.h"

SCENARIO("Guests hold the host's difficulty and get their own back on leaving", "[session][settings]")
{
    Party party(2, [](FakeEngine& aEngine) { aEngine.Difficulty = aEngine.Name == "Dovahkiin" ? 4 : 1; });
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    FakeEngine& other = Party::Engine(party.Guest(1));

    CHECK(host.Difficulty == 4);
    CHECK(guest.Difficulty == 4);
    CHECK(other.Difficulty == 4);
    CHECK(host.GreetDistance == 0.f);
    CHECK(guest.GreetDistance == 0.f);
    CHECK(host.DeathReloadHeld);
    CHECK(guest.DeathReloadHeld);

    party.As(party.Host(), [&] { host.ChoosesDifficulty(5); });
    party.Until([&] { return guest.Difficulty == 5 && other.Difficulty == 5; },
                "the host's changed difficulty reaches both guests");
    CHECK(host.Difficulty == 5);

    party.As(party.Guest(), [&] { guest.ChoosesDifficulty(0); });
    party.Step(party.Guest());
    CHECK(guest.Difficulty == 5);

    party.Leave(party.Guest());
    party.Until([&] { return !party.Guest().GetTransport().IsOnline() && guest.Difficulty == 1; },
                "a leaving guest gets its original difficulty back");
    CHECK(guest.GreetDistance == 150.f);
    CHECK_FALSE(guest.DeathReloadHeld);
    CHECK(host.Difficulty == 5);
    CHECK(other.Difficulty == 5);

    party.Join(party.Guest());
    CHECK(guest.Difficulty == 5);
    CHECK(guest.GreetDistance == 0.f);
    CHECK(guest.DeathReloadHeld);
    party.Leave(party.Guest());
    party.Until([&] { return !party.Guest().GetTransport().IsOnline() && guest.Difficulty == 1; },
                "rejoining did not replace the guest's original difficulty");
}
