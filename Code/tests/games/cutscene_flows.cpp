// Cutscenes for the whole party across games (docs/design/cutscenes.md): a world script's controls reach every game,
// and a leaver's cutscene gives back what it held.
#include "Party.h"

using namespace PartyRules;

SCENARIO("A world script on the guest takes everyone's controls; the guest leaves; the host gets them back",
         "[cutscene]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    WHEN("a world script on the guest takes the controls")
    {
        party.WorldScript(guest, DisablePlayerControls());
        party.Until([&] { return !Party::Engine(host).Has(kMovement); }, "the host's movement is taken");

        THEN("both games' movement and fighting are taken, held by the guest's game on the host")
        {
            CHECK(!Party::Engine(guest).Has(kMovement | kFighting));
            CHECK(!Party::Engine(host).Has(kFighting));
            CHECK(Party::Engine(host).Holds.HeldBy(kThisGame) == 0);
            CHECK(Party::Engine(host).Holds.Held() ==
                  0x541); // movement and jumping, fighting, menus: as Skyrim takes them
        }

        AND_WHEN("the guest leaves mid-cutscene")
        {
            party.Leave(guest);
            party.Until([&] { return Party::Engine(host).Has(kMovement | kFighting | kMainFour); },
                        "the host's controls come back");

            THEN("nothing is held on the host") CHECK(Party::Engine(host).Holds.Held() == 0);
        }
    }
}

SCENARIO("A world script on the host takes everyone's controls; the guest leaves and gets its own back", "[cutscene]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    party.WorldScript(host, DisablePlayerControls());
    party.Until([&] { return !Party::Engine(guest).Has(kMovement); }, "the guest's movement is taken");

    WHEN("the guest leaves")
    {
        party.Leave(guest);
        party.Until([&] { return Party::Engine(guest).Has(kMovement | kFighting); }, "the guest's controls come back");

        THEN("the host's cutscene still holds the host's") CHECK(!Party::Engine(host).Has(kMovement));
        AND_WHEN("the host's script gives them back")
        {
            party.WorldScript(host, EnablePlayerControls());
            THEN("the host's come back") CHECK(Party::Engine(host).Has(kMovement | kFighting));
        }
    }
}

SCENARIO("Two cutscenes at once: the first to end gives back only its own", "[cutscene]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    party.WorldScript(host, DisablePlayerControls());
    party.WorldScript(guest, DisablePlayerControls(true, false, false, false, true));
    party.Until([&] { return !Party::Engine(host).Has(kSneaking) && !Party::Engine(guest).Has(kFighting); },
                "each game's cutscene reaches the other");

    WHEN("the host's cutscene ends first")
    {
        party.WorldScript(host, EnablePlayerControls());
        party.Until([&] { return Party::Engine(guest).Has(kFighting); }, "the guest's fighting comes back");

        THEN("movement stays taken on both: the guest's cutscene holds it")
        {
            CHECK(!Party::Engine(host).Has(kMovement));
            CHECK(!Party::Engine(guest).Has(kMovement));
            CHECK(Party::Engine(host).Has(kFighting));
        }
    }
}

SCENARIO("A restriction stays with its game", "[cutscene]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    WHEN("a world script on the host takes fighting only")
    {
        party.WorldScript(host, DisablePlayerControls(false, true, false, false, false, false, false));
        // A marker after it: the guest has had every message the host sent before.
        party.WorldScript(host, DisablePlayerControls(true, false, false, false, false, false, false));
        party.Until([&] { return !Party::Engine(guest).Has(kMovement); }, "the marker reaches the guest");

        THEN("the guest can still fight")
        {
            CHECK(!Party::Engine(host).Has(kFighting));
            CHECK(Party::Engine(guest).Has(kFighting));
        }
    }
}
