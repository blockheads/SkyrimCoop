#include <catch2/catch.hpp>
#include <Services/PartyMarkers.h>
#include <limits>

TEST_CASE("Party compass bearings cross north and reject an undefined direction", "[party-markers]")
{
    const glm::vec3 cOrigin{0.f};
    CHECK(*PartyMarkers::Bearing(cOrigin, {0.f, 10.f, 0.f}) == Approx(0));
    CHECK(*PartyMarkers::Bearing(cOrigin, {10.f, 0.f, 0.f}) == Approx(90));
    CHECK(*PartyMarkers::Bearing(cOrigin, {0.f, -10.f, 0.f}) == Approx(180));
    CHECK(*PartyMarkers::Bearing(cOrigin, {-10.f, 0.f, 0.f}) == Approx(270));
    CHECK(*PartyMarkers::Bearing(cOrigin, {-0.1f, 10.f, 0.f}) > 359);
    CHECK(*PartyMarkers::Bearing(cOrigin, {0.1f, 10.f, 0.f}) < 1);
    CHECK_FALSE(PartyMarkers::Bearing(cOrigin, {0.f, 0.f, 100.f}));
    CHECK_FALSE(PartyMarkers::Bearing(cOrigin, {std::numeric_limits<float>::infinity(), 0.f, 0.f}));
    CHECK_FALSE(PartyMarkers::Bearing(cOrigin, {0.f, std::numeric_limits<float>::quiet_NaN(), 0.f}));
}

TEST_CASE("Party directions never compare coordinates from unrelated spaces", "[party-markers]")
{
    PartyMarkerSet set;
    set.PlayerWorldSpaceId = 0x3C;
    set.PlayerCellId = 1;
    PartyMarker marker;
    marker.WorldSpaceId = 0x3C;
    marker.CellId = 2;
    CHECK(PartyMarkers::SharesSpace(set, marker));
    marker.WorldSpaceId = 0x99;
    CHECK_FALSE(PartyMarkers::SharesSpace(set, marker));
    marker.WorldSpaceId = 0;
    CHECK_FALSE(PartyMarkers::SharesSpace(set, marker));
    set.PlayerWorldSpaceId = 0;
    CHECK_FALSE(PartyMarkers::SharesSpace(set, marker));
    marker.CellId = 1;
    CHECK(PartyMarkers::SharesSpace(set, marker));
    set.PlayerCellId = marker.CellId = 0;
    CHECK_FALSE(PartyMarkers::SharesSpace(set, marker));
}
