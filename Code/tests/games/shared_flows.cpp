// What the party shares besides the world: map markers (MapService) and the clock's speed (TimeScaleService).
#include "Party.h"

namespace
{
constexpr uint32_t kTamriel = 0x0000003C;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x00031D2E;
    aEngine.LoadCell(0x00031D2E);
}
} // namespace

SCENARIO("A player's map marker shows on everyone's map", "[shared]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));

    party.As(party.Guest(0), [&] { guest.PlacesMarker({1000.f, 2000.f, 30.f}, kTamriel); });
    party.Until([&] { return host.Waypoint && other.Waypoint; }, "the host and the other guest have the marker");
    THEN("where it was placed")
    {
        CHECK(host.Waypoint->WorldSpace == kTamriel);
        CHECK(glm::distance(other.Waypoint->Position, glm::vec3(1000.f, 2000.f, 30.f)) < 1.f);
    }

    WHEN("its player removes it")
    {
        party.As(party.Guest(0), [&] { guest.RemovesMarker(); });
        party.Until([&] { return !host.Waypoint && !other.Waypoint; }, "it goes from every map");
    }
}

SCENARIO("A player's slowed clock slows everyone's", "[shared]")
{
    Party party(1, Plugins);
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
