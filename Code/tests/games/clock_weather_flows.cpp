// The host's clock and weather on the guests' games (HostClock and HostWeather, GuestClock and GuestWeather).
#include "Party.h"
#include "Fixtures.h"

namespace
{
const GameId kClear{0, 0x0010A23A}; // SkyrimClear
const GameId kStorm{0, 0x0010A241}; // SkyrimStormRain
const GameId kSnow{0, 0x000C8221};  // a snowstorm: Winterhold's list holds it, Riverwood's not
constexpr uint32_t kWinterhold = 0x10FDC6;
constexpr uint32_t kRiverwood = 0x2A72D;

// Both games' regions (the same plugins): their weather lists.
void Regions(FakeEngine& aEngine)
{
    aEngine.RegionLists = {{kWinterhold, {kSnow, kClear}}, {kRiverwood, {kClear, kStorm}}};
}


} // namespace

SCENARIO("The host's weather holds on the guests' games", "[world]")
{
    Party party(1, SmokeWorld, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    host.Weather = kClear;

    party.Join(party.Guest());
    party.Until([&] { return guest.WeatherHeld && guest.Weather == kClear; },
                "a joining guest gets the host's weather");

    host.Weather = kStorm;
    party.Until([&] { return guest.Weather == kStorm; }, "the host's change of weather reaches it");

    party.Leave(party.Guest());
    party.Until([&] { return !guest.WeatherHeld; }, "leaving gives the guest's game its own weather back");
}

SCENARIO("A guest shows the weather the host gives its own region, faded into", "[world]")
{
    Party party(1, SmokeWorld, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    Regions(host);
    Regions(guest);
    host.WeatherRegion = guest.WeatherRegion = kWinterhold;
    host.ShowWeather(kClear);
    guest.ShowWeather(kClear);

    party.Join(party.Guest());
    party.Until([&] { return guest.WeatherHeld && guest.Weather == kClear; }, "a joining guest gets the host's weather");

    host.ShowWeather(kSnow);
    party.Until([&] { return guest.Weather == kSnow; }, "in the host's region, the host's snow");

    // The guest walks off to Riverwood, whose list has no snow: the host's game holds clear for it.
    guest.WeatherRegion = kRiverwood;
    party.Until([&] { return guest.Weather == kClear; }, "in its own region, the weather the host's game gives Riverwood");
    // Past the override's interval: still the held weather, never its game's own roll between.
    party.Until([&] { return !guest.LastWeather; }, "the fade into Riverwood's weather ran its course");
    const size_t cChanges = guest.WeatherChanges;
    for (uint32_t frame = 0; frame < 3 * FakeEngine::kOverrideExpiryFrames; ++frame)
        party.Frame();
    CHECK(guest.WeatherChanges == cChanges);
    CHECK(guest.Weather == kClear);

    // A script's or the console's forced weather wins everywhere, and once released, the region's own again.
    host.ForcedWeather = kStorm;
    host.ShowWeather(kStorm);
    party.Until([&] { return guest.Weather == kStorm; }, "the host's forced weather reaches it anywhere");
    host.ForcedWeather = {};
    host.ShowWeather(kSnow);
    party.Until([&] { return guest.Weather == kClear; }, "released: Riverwood's own again");

    guest.WeatherRegion = kWinterhold;
    party.Until([&] { return guest.Weather == kSnow; }, "back in the host's region, the host's snow");
    CHECK(guest.WeatherCuts == 0);
}

SCENARIO("The host's calendar runs on the guests' games", "[world]")
{
    Party party(1, SmokeWorld, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    host.Calendar.Day = 17;
    host.Calendar.Month = 8;
    host.Calendar.DaysPassed = 230;

    party.Join(party.Guest());
    party.Until(
        [&] { return guest.Calendar.DaysPassed == 230 && guest.Calendar.Day == 17 && guest.Calendar.Month == 8; },
        "a joining guest takes the host's date");

    // A wait of 20 hours on the host, into the next day.
    host.Calendar.Time += 20.f;
    if (host.Calendar.Time >= 24.f)
    {
        host.Calendar.Time -= 24.f;
        ++host.Calendar.Day;
        ++host.Calendar.DaysPassed;
    }
    party.Until([&] { return guest.Calendar.DaysPassed == 231 && guest.Calendar.Day == 18; },
                "the host's wait reaches the guest");
}
