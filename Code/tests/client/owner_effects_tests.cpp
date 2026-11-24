// A copy answers "has this effect" with its owner's active effects (Games/OwnerEffects.h).
#include <catch2/catch.hpp>

#include <Games/OwnerEffects.h>

TEST_CASE("A copy has an effect, or one with a keyword, when its owner holds it", "[OwnerEffects]")
{
    const std::vector<uint32_t> cHeld{0x13CA9, 0x9F28F}; // sorted
    CHECK(OwnerEffects::Has(cHeld, 0x9F28F));
    CHECK_FALSE(OwnerEffects::Has(cHeld, 0x5A1A6));
    CHECK_FALSE(OwnerEffects::Has({}, 0x9F28F));
    // The gas trap's check: its poison carries the gas keyword.
    CHECK(OwnerEffects::HasWithKeyword(cHeld, [](uint32_t aId) { return aId == 0x9F28F; }));
    CHECK_FALSE(OwnerEffects::HasWithKeyword(cHeld, [](uint32_t) { return false; }));
    // A hazard's spell (IsSpellTarget): one of its effects.
    CHECK(OwnerEffects::HasAny(cHeld, {0x5A1A6, 0x9F28F}));
    CHECK_FALSE(OwnerEffects::HasAny(cHeld, {0x5A1A6}));
}
