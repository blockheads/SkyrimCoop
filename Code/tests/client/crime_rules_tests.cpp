// CrimeBridge::Judge's checks (docs/re/systems/crime.md, "A created item's theft names its owner").
#include <catch2/catch.hpp>

#include <Services/Host/CrimeRules.h>

using namespace CrimeRules;

TEST_CASE("A theft of a placed item is checked against the item, in the guest's cell", "[CrimeRules]")
{
    for (const auto cAction : {CrimeAction::Theft, CrimeAction::ContainerTheft, CrimeAction::Lockpick})
    {
        const auto cChecks = ChecksFor(cAction, false);
        CHECK(cChecks.SameCell);
        CHECK(cChecks.Ownership);
    }
}

TEST_CASE("A created item's theft names its owner actor: in any loaded cell, no ownership check", "[CrimeRules]")
{
    const auto cChecks = ChecksFor(CrimeAction::Theft, true);
    CHECK_FALSE(cChecks.SameCell);
    CHECK_FALSE(cChecks.Ownership);
}

TEST_CASE("A pickpocket is left to its own alarm, in the guest's cell", "[CrimeRules]")
{
    const auto cChecks = ChecksFor(CrimeAction::Pickpocket, true);
    CHECK(cChecks.SameCell);
    CHECK_FALSE(cChecks.Ownership);
}

TEST_CASE("The alarm gets the owner the guest's alarm was given, else the target's", "[CrimeRules]")
{
    CHECK(AlarmOwner(0x13479u, 0u) == 0x13479u);
    CHECK(AlarmOwner(0x13479u, 0x5u) == 0x13479u);
    CHECK(AlarmOwner(0u, 0x5u) == 0x5u);
}

TEST_CASE("A guest's seen theft alarms at once: the steal warning would go to the host's player", "[CrimeRules]")
{
    CHECK_FALSE(Warns(true, true));
    CHECK_FALSE(Warns(false, true));
    CHECK(Warns(true, false));
}
