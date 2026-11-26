#pragma once

#include <Structs/GameId.h>
#include <Structs/TimeModel.h>

#include <cstdint>
#include <functional>
#include <optional>

// The host's sky (weather.md): the weather shown, whether a script or the console forced it (Sky's override), and the
// host's player's weather region (none: an interior without a sky, or none known since a set weather).
struct HostSky
{
    GameId Weather;
    bool Forced{};
    GameId Region;

    bool operator==(const HostSky&) const noexcept = default;
};

// What the host's rules read and change in its game: the host's clock and sky. A part of HostGame.
struct HostTimeGame
{
    // The host's clock (the session's) and setting its hour.
    std::function<TimeModel()> ReadTime;
    std::function<void(float aHours)> SetHour;
    std::function<void(float aHours)> AdvanceTime;
    // The host's weather, when it shows one to share (none: no sky, or the map's).
    std::function<std::optional<HostSky>()> ReadWeather;
    // A region's own weather in the host's game (TESRegion's, 16448: the shown one where its list holds it, else its
    // roll); none for no such region, or one without weathers.
    std::function<std::optional<GameId>(const GameId& acRegion)> RegionWeather;
};
