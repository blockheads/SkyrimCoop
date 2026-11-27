#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Wire/Buffer.h>

// A guest's player's weather region now (Sky::PlayerWeatherRegion; none: an interior without a sky, or none known).
struct WeatherRegionRequest final : MessageOf<WeatherRegionRequest, kWeatherRegionRequest>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Region); }
    bool operator==(const WeatherRegionRequest&) const noexcept = default;

    GameId Region{};
};
