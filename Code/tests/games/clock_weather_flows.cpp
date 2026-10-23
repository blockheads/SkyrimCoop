// The host's clock and weather on the guests' games (HostClock and HostWeather, GuestClock and GuestWeather).
#include "Party.h"

namespace
{
const GameId kClear{0, 0x0010A23A}; // SkyrimClear
const GameId kStorm{0, 0x0010A241}; // SkyrimStormRain

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x00031D2E;
    aEngine.LoadCell(0x00031D2E);
}
} // namespace

SCENARIO("The host's weather holds on the guests' games", "[world]")
{
    Party party(1, Plugins, false);
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

SCENARIO("The host's calendar runs on the guests' games", "[world]")
{
    Party party(1, Plugins, false);
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
