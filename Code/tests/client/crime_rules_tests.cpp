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

TEST_CASE("An NPC is hostile for a party crime for any grudge Skyrim keeps", "[CrimeRules]")
{
    CHECK_FALSE(PartyIsCriminalTo({}));
    CHECK(PartyIsCriminalTo({.FactionEnemy = true}));
    CHECK(PartyIsCriminalTo({.Victim = true}));
    // A theft's witness: angry (39631) with no faction flag and no crime against it (crime.md).
    CHECK(PartyIsCriminalTo({.AngryWithPlayer = true}));
    // A guard answering the alarm (39695): on the crime's witness list, nothing else.
    CHECK(PartyIsCriminalTo({.Witness = true}));
}

TEST_CASE("A guest's steal warning names the guest's copy where the engine names its player", "[CrimeRules]")
{
    constexpr int cPlayer = 1, cCopy = 2, cOwner = 3;
    CHECK(Warned(cPlayer, cPlayer, cCopy) == cCopy);
    CHECK(Warned(cOwner, cPlayer, cCopy) == cOwner); // only the player's place
    CHECK(Warned(cPlayer, cPlayer, 0) == cPlayer);   // the host's own theft
}

TEST_CASE("A guest's pending crime is looked at from the guest's copy, the host's own as the engine does", "[CrimeRules]")
{
    constexpr uintptr_t cWitness = 10, cOther = 11, cCrime = 100, cLater = 101;
    constexpr uint32_t cCopy = 0xFF000800;
    PendingCriminals pending;
    pending.Set(cWitness, cCrime, cCopy); // StealAlarm in Judge's scope (39557)
    CHECK(pending.Of(cWitness, cCrime) == cCopy);
    CHECK(pending.Of(cOther, cCrime) == 0);

    SECTION("a later crime of the host's replaces it")
    {
        pending.Set(cWitness, cLater, 0);
        CHECK(pending.Of(cWitness, cLater) == 0);
        CHECK(pending.Of(cWitness, cCrime) == 0);
        CHECK(pending.Size() == 0);
    }
    SECTION("a crime the engine put there without the hook is not the guest's")
    {
        CHECK(pending.Of(cWitness, cLater) == 0);
        CHECK(pending.Of(cWitness, 0) == 0);
    }
    SECTION("judged, the entry goes; still pending, it stays")
    {
        pending.Judged(cWitness, cCrime);
        CHECK(pending.Of(cWitness, cCrime) == cCopy);
        pending.Judged(cWitness, 0);
        CHECK(pending.Size() == 0);
    }
}
