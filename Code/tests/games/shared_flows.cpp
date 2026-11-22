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
