// A copy answers about its effects with its owner's (Games/OwnerEffects.h).
#include <catch2/catch.hpp>

#include <Games/OwnerEffects.h>

using OwnerEffects::Held;

TEST_CASE("A copy has an effect, a spell, or one with a keyword, when its owner holds it", "[OwnerEffects]")
{
    const std::vector<Held> cHeld{{0x12FCD, 0x13CA9}, {0xA0370, 0x9F28F}};
    CHECK(OwnerEffects::Has(cHeld, 0x9F28F));
    CHECK_FALSE(OwnerEffects::Has(cHeld, 0x5A1A6));
    CHECK(OwnerEffects::HasSpell(cHeld, 0xA0370));
    // Two spells sharing an effect: only the held spell is (IsSpellTarget compares the spell, ActiveEffect +0x40).
    CHECK_FALSE(OwnerEffects::HasSpell(cHeld, 0xA0371));
    // The gas trap's check: its poison carries the gas keyword; the engine's out value is its spell.
    const Held* pFound = OwnerEffects::WithKeyword(cHeld, [](uint32_t aId) { return aId == 0x9F28F; });
    REQUIRE(pFound);
    CHECK(pFound->Spell == 0xA0370);
    CHECK_FALSE(OwnerEffects::WithKeyword(cHeld, [](uint32_t) { return false; }));
}
