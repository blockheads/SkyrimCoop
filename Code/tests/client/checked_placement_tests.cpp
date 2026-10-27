#include <catch2/catch.hpp>
#include <Games/CheckedPlacement.h>
#include <cmath>

TEST_CASE("A joining player gets a checked spot beside the host", "[checked-placement]")
{
    const glm::vec3 cHost(10.f, 20.f, 30.f);
    bool floor = true, room = true, queries = true;
    float slope = 1.f;
    int calls = 0;
    auto cast = [&](const glm::vec3& acFrom, const glm::vec3& acTo) -> std::optional<CheckedPlacement::Hit> {
        ++calls;
        if (!queries) return std::nullopt;
        if (acTo.z < acFrom.z - 200.f)
            return CheckedPlacement::Hit{floor, {acFrom.x, acFrom.y, cHost.z}, {0.f, 0.f, slope}};
        return CheckedPlacement::Hit{!room, acTo};
    };
    SECTION("floor and clearance leave room for a player")
    {
        const auto cSpot = CheckedPlacement::Beside(cHost, cast);
        REQUIRE(cSpot);
        CHECK(std::hypot(cSpot->x - cHost.x, cSpot->y - cHost.y) >= 100.f);
        CHECK(cSpot->z == cHost.z + 5.f);
        CHECK(calls > 10);
    }
    SECTION("an unsupported spot is refused") { floor = false; CHECK_FALSE(CheckedPlacement::Beside(cHost, cast)); }
    SECTION("a steep floor is refused") { slope = 0.3f; CHECK_FALSE(CheckedPlacement::Beside(cHost, cast)); }
    SECTION("a blocked capsule or path is refused") { room = false; CHECK_FALSE(CheckedPlacement::Beside(cHost, cast)); }
    SECTION("missing physics is refused") { queries = false; CHECK_FALSE(CheckedPlacement::Beside(cHost, cast)); }
}

TEST_CASE("Checked arrival reroutes blocked bearings and rejects incomplete clearance", "[checked-placement]")
{
    const glm::vec3 cHost(0.f);
    int floors = 0;
    auto cast = [&](const glm::vec3& acFrom, const glm::vec3& acTo) -> std::optional<CheckedPlacement::Hit> {
        if (acTo.z < acFrom.z - 200.f)
        {
            ++floors;
            return CheckedPlacement::Hit{floors > 1, {acFrom.x, acFrom.y, 0.f}, {0.f, 0.f, 1.f}};
        }
        return CheckedPlacement::Hit{};
    };
    REQUIRE(CheckedPlacement::Beside(cHost, cast));
    CHECK(floors == 2);
    const auto cIncomplete = [&](const glm::vec3& acFrom, const glm::vec3& acTo) -> std::optional<CheckedPlacement::Hit> {
        if (acTo.z < acFrom.z - 200.f)
            return CheckedPlacement::Hit{true, {acFrom.x, acFrom.y, 0.f}, {0.f, 0.f, 1.f}};
        return std::nullopt;
    };
    CHECK_FALSE(CheckedPlacement::Beside(cHost, cIncomplete));
}
