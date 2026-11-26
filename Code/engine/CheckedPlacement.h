#pragma once

#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <cmath>
#include <optional>
#include <initializer_list>

namespace CheckedPlacement
{
struct Hit
{
    bool Blocked{};
    glm::vec3 Point{};
    glm::vec3 Normal{};
};

// Cast returns no value when the query could not run; an unblocked ray is a completed query.
template <class Cast>
std::optional<glm::vec3> Beside(const glm::vec3& acAnchor, Cast&& aCast)
{
    constexpr float cPi = 3.14159265358979323846f;
    for (int step = 0; step < 18; ++step)
    {
        const float cAngle = (90.f + 20.f * step) * cPi / 180.f;
        const glm::vec3 cOffset(std::sin(cAngle) * 120.f, std::cos(cAngle) * 120.f, 0.f);
        const auto cFloor = aCast(acAnchor + cOffset + glm::vec3(0.f, 0.f, 100.f),
                                  acAnchor + cOffset - glm::vec3(0.f, 0.f, 200.f));
        if (!cFloor) return std::nullopt;
        if (!cFloor->Blocked || cFloor->Normal.z < 0.7f || std::abs(cFloor->Point.z - acAnchor.z) > 75.f)
            continue;
        const glm::vec3 cFeet = cFloor->Point + glm::vec3(0.f, 0.f, 5.f);
        bool clear = true;
        const auto cClear = [&](const glm::vec3& acFrom, const glm::vec3& acTo) {
            const auto cHit = aCast(acFrom, acTo);
            return cHit && !cHit->Blocked;
        };
        clear = cClear(cFeet + glm::vec3(0.f, 0.f, 15.f), cFeet + glm::vec3(0.f, 0.f, 140.f));
        for (float height : {30.f, 90.f})
            for (int around = 0; clear && around < 8; ++around)
            {
                const float cRay = around * cPi / 4.f;
                const glm::vec3 cStart = cFeet + glm::vec3(0.f, 0.f, height);
                clear = cClear(cStart, cStart + glm::vec3(std::sin(cRay) * 35.f, std::cos(cRay) * 35.f, 0.f));
            }
        if (clear && cClear(acAnchor + glm::vec3(0.f, 0.f, 100.f), cFeet + glm::vec3(0.f, 0.f, 100.f)))
            return cFeet;
    }
    return std::nullopt;
}
} // namespace CheckedPlacement
