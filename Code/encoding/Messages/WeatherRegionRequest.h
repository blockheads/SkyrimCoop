#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Wire/Buffer.h>

// A guest's player's weather region now (Sky::PlayerWeatherRegion; none: an interior without a sky, or none known).
struct WeatherRegionRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kWeatherRegionRequest;

    WeatherRegionRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const WeatherRegionRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Region == acRhs.Region; }

    GameId Region{};
};
