// What the party shares besides the world: map markers (MapService) and the clock's speed (TimeScaleService).
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kTamriel = 0x0000003C;


} // namespace

SCENARIO("A player's map marker shows on everyone's map", "[shared]")
{
    Party party(2, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));

    party.As(party.Guest(0), [&] { guest.PlacesMarker({1000.f, 2000.f, 30.f}, kTamriel); });
    party.Until([&] { return host.Waypoint && other.Waypoint; }, "the host and the other guest have the marker");
    INFO("where it was placed");
    {
        CHECK(host.Waypoint->WorldSpace == kTamriel);
        CHECK(glm::distance(other.Waypoint->Position, glm::vec3(1000.f, 2000.f, 30.f)) < 1.f);
    }

    INFO("its player removes it");
    {
        party.As(party.Guest(0), [&] { guest.RemovesMarker(); });
        party.Until([&] { return !host.Waypoint && !other.Waypoint; }, "it goes from every map");
    }
}

SCENARIO("A guest joining after a marker was placed has it on its map", "[shared][join]")
{
    Party party(2, SmokeWorld, false);
    party.Join(party.Guest(0));
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& late = Party::Engine(party.Guest(1));

    party.As(party.Host(), [&] { host.PlacesMarker({1000.f, 2000.f, 30.f}, kTamriel); });
    party.Until([&] { return Party::Engine(party.Guest(0)).Waypoint.has_value(); }, "the first guest has the marker");

    party.Join(party.Guest(1));
    party.Until([&] { return late.Waypoint.has_value(); }, "the late guest has it");
    CHECK(late.Waypoint->WorldSpace == kTamriel);
    CHECK(glm::distance(late.Waypoint->Position, glm::vec3(1000.f, 2000.f, 30.f)) < 1.f);
}

SCENARIO("A player's slowed clock slows everyone's", "[shared]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    // A kill cam on the guest's game.
    party.As(party.Guest(), [&] { guest.SlowsTime(0.25f); });
    party.Until([&] { return host.TimeMultiplier() == 0.25f; }, "the host's clock runs at the kill cam's speed");
    CHECK(guest.TimeMultiplier() == 0.25f);

    party.As(party.Guest(), [&] { guest.SlowsTime(1.f); });
    party.Until([&] { return host.TimeMultiplier() == 1.f && guest.TimeMultiplier() == 1.f; },
                "both run at full speed when it ends");
}

SCENARIO("A guest's down camera slows its own clock alone", "[shared]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    // Knocked down or frozen (Ice Form's paralysis): vanilla's down camera runs its clock at 0.8.
    party.As(party.Guest(), [&] { guest.EntersDownCamera(); });
    // A marker the guest places after it: once the host has it, it heard all the guest said before.
    party.As(party.Guest(), [&] { guest.PlacesMarker({1000.f, 2000.f, 30.f}, kTamriel); });
    party.Until([&] { return host.Waypoint.has_value(); }, "the host hears the guest's marker");
    CHECK(guest.TimeMultiplier() == 0.8f);
    CHECK(host.TimeMultiplier() == 1.f);

    INFO("a kill cam on the host's game meanwhile still slows both");
    {
        party.As(party.Host(), [&] { host.SlowsTime(0.25f); });
        party.Until([&] { return guest.TimeMultiplier() == 0.25f; }, "the guest's clock runs at the kill cam's speed");
        party.As(party.Host(), [&] { host.SlowsTime(1.f); });
        party.Until([&] { return guest.TimeMultiplier() == 0.8f; }, "the guest's back at its down camera's speed");
    }

    INFO("a kill cam on the guest's own game while it lies slows both");
    {
        party.As(party.Guest(), [&] { guest.SlowsTime(0.25f); });
        party.Until([&] { return host.TimeMultiplier() == 0.25f; }, "the host's clock runs at the guest's kill cam's speed");
        // Its end asks 1, which replaces the down camera's 0.8 in vanilla too.
        party.As(party.Guest(), [&] { guest.SlowsTime(1.f); });
        party.Until([&] { return host.TimeMultiplier() == 1.f; }, "the host's back at full speed");
        CHECK(guest.TimeMultiplier() == 1.f);
    }

    INFO("it gets up");
    {
        party.As(party.Guest(), [&] { guest.LeavesDownCamera(); });
        party.As(party.Guest(), [&] { guest.RemovesMarker(); });
        party.Until([&] { return !host.Waypoint; }, "the host hears the marker go");
        CHECK(guest.TimeMultiplier() == 1.f);
        CHECK(host.TimeMultiplier() == 1.f);
    }
}

SCENARIO("A guest knocked down in its own Slow Time frees the host's clock", "[shared]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    party.As(party.Guest(), [&] { guest.SlowsTime(0.3f); });
    party.Until([&] { return host.TimeMultiplier() == 0.3f; }, "the host's clock runs at the guest's Slow Time");
    // The down camera's 0.8 replaces the Slow Time's slot (44300), as in vanilla.
    party.As(party.Guest(), [&] { guest.EntersDownCamera(); });
    party.Until([&] { return host.TimeMultiplier() == 1.f; }, "the host's clock is free");
    CHECK(guest.TimeMultiplier() == 0.8f);
    party.As(party.Guest(), [&] { guest.LeavesDownCamera(); });
    CHECK(guest.TimeMultiplier() == 1.f);
}
