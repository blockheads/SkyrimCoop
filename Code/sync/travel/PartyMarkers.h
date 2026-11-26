#pragma once

#include <glm/vec3.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct PartyMarker
{
    uint32_t PlayerId{};
    uint32_t FormId{};
    std::string Name;
    uint32_t CellId{};
    uint32_t WorldSpaceId{};
    glm::vec3 Position{};
    float Heading{};
};

struct PartyMarkerSet
{
    uint32_t PlayerCellId{};
    uint32_t PlayerWorldSpaceId{};
    glm::vec3 PlayerPosition{};
    std::vector<PartyMarker> Members;
};

namespace PartyMarkers
{
inline bool SharesSpace(const PartyMarkerSet& acSet, const PartyMarker& acMarker) noexcept
{
    return acSet.PlayerWorldSpaceId ? acSet.PlayerWorldSpaceId == acMarker.WorldSpaceId
        : !acMarker.WorldSpaceId && acSet.PlayerCellId && acSet.PlayerCellId == acMarker.CellId;
}

// Skyrim's compass headings are degrees clockwise from north.
inline std::optional<double> Bearing(const glm::vec3& acFrom, const glm::vec3& acTo) noexcept
{
    const double cX = double(acTo.x) - acFrom.x, cY = double(acTo.y) - acFrom.y;
    if (!std::isfinite(cX) || !std::isfinite(cY) || (cX == 0 && cY == 0))
        return std::nullopt;
    double heading = std::atan2(cX, cY) * (180.0 / 3.14159265358979323846);
    if (heading < 0) heading += 360;
    return heading;
}
}
