// CrimeBridge::Judge's ownership recheck (docs/re/systems/crime.md, "A created item's theft names its owner").
#include <catch2/catch.hpp>

#include <Games/Skyrim/CrimeRules.h>

using CrimeRules::RechecksOwnership;

TEST_CASE("A theft of a placed item is rechecked against the item", "[CrimeRules]")
{
    CHECK(RechecksOwnership(CrimeAction::Theft, false));
    CHECK(RechecksOwnership(CrimeAction::ContainerTheft, false));
    CHECK(RechecksOwnership(CrimeAction::Lockpick, false));
}

TEST_CASE("A created item's theft names its owner actor: no recheck, which an NPC always passes", "[CrimeRules]")
{
    CHECK_FALSE(RechecksOwnership(CrimeAction::Theft, true));
}

TEST_CASE("A pickpocket is left to its own alarm", "[CrimeRules]")
{
    CHECK_FALSE(RechecksOwnership(CrimeAction::Pickpocket, true));
}
