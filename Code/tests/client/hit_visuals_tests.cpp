#include <catch2/catch.hpp>
#include <Games/HitVisualsRules.h>

TEST_CASE("Instant FX Persist visuals flash and can play on a copy", "[instant-visuals]")
{
    using namespace HitVisualsRules;
    CHECK(Flash(kPersist, true, 0.f, 1));
    CHECK(Allowed(kPersist, false));
    CHECK_FALSE(Flash(kPersist, true, 10.f, 1));
    CHECK_FALSE(Flash(kPersist, true, 0.f, 0)); // a constant ability is not an instant effect
    CHECK_FALSE(Flash(kPersist, true, 0.f, 2)); // concentration lasts until the cast stops
    CHECK_FALSE(Flash(kNoHit, true, 0.f, 1));
    CHECK_FALSE(Flash(kPersist, false, 0.f, 1));
    CHECK(Flash(0, true, 3600.f, 1)); // Gift of Charity's visuals play once during its hour
    CHECK_FALSE(Allowed(0, true));
    CHECK_FALSE(Allowed(kNoHit | kPersist, false));
}
