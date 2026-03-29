#include <catch2/catch.hpp>
#include "replay/replay_harness.h"
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string FixturePath(const char* aName) {
    return std::string(FIXTURE_PATH) + "/" + aName;
}

TEST_CASE("Weather set_weather and force_weather update cached weather ID", "[Replay][WeatherService]")
{
    ReplayHarness harness;
    size_t events = harness.ReplayFile(FixturePath("weather_change.ndjson"));

    REQUIRE(events == 4);

    // 983905 = 0xF0361, the weather_form_id in the fixture
    REQUIRE(harness.GetWeatherService().GetCachedWeatherId() == 983905);

    // sky_ptr sentinel stored by weather_change op
    REQUIRE(harness.GetPointerTable().Contains(0xFFFF0001));
}
