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

TEST_CASE("A copy answers each effect query as the engine's visitors read the owner's list", "[OwnerEffects]")
{
    // 34578 skips a dispelled entry and the Usage Monitor effect; 34576 returns the first keyword match in list order.
    using OwnerEffects::Held;
    CHECK_FALSE(OwnerEffects::CountsAsSpellTarget(0x40000, 0x9F28F));
    CHECK_FALSE(OwnerEffects::CountsAsSpellTarget(0, 0x14B));
    CHECK(OwnerEffects::CountsAsSpellTarget(0, 0x9F28F));
    const std::vector<Held> cDispelled{{0xA0370, 0x9F28F, false}};
    CHECK(OwnerEffects::Has(cDispelled, 0x9F28F)); // HasMagicEffect counts it
    CHECK_FALSE(OwnerEffects::HasSpell(cDispelled, 0xA0370));
    const std::vector<Held> cReversed{{0xB0002, 0x9F28F}, {0xA0001, 0x9F290}}; // the owner's order, higher id first
    const Held* pFirst = OwnerEffects::WithKeyword(cReversed, [](uint32_t) { return true; });
    REQUIRE(pFirst);
    CHECK(pFirst->Spell == 0xB0002);
}
