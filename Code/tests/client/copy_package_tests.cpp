// HookInitFromPackage's rule (actor-ai-and-copies.md): a copy runs its owner's package, never one its own game picks.
#include <catch2/catch.hpp>

#include <algorithm>

#include <Games/Skyrim/CopyCombat.h>
#include <Games/Skyrim/CopyPackage.h>

namespace
{
constexpr uint32_t cFollow = 0x5C84B;         // PlayerFollowerPackage
constexpr uint32_t cCombatOverride = 0x5C851; // PlayerFollowerCombatOverridePackage
constexpr uint32_t cDoNothing = 0x100770;
} // namespace

TEST_CASE("A copy keeps its owner's package when its own game puts a combat override on it", "[CopyPackage]")
{
    CHECK(CopyPackage::Put(true, cFollow, cCombatOverride) == cFollow);
    CHECK(CopyPackage::Put(true, cFollow, cFollow) == cFollow);
}

TEST_CASE("A copy with no owner package yet takes the engine's (its do-nothing one)", "[CopyPackage]")
{
    CHECK(CopyPackage::Put(true, 0, cDoNothing) == cDoNothing);
}

TEST_CASE("A copy's do-nothing package is made as the engine makes its own", "[CopyPackage]")
{
    struct Package
    {
        uint32_t packFlags = 0;
        int32_t Procedure = -1;
    } package;
    const auto* pMade = CopyPackage::CreateDoNothing([&] { return &package; }, [](Package* apPackage) { apPackage->Procedure = 23; });
    CHECK(pMade->packFlags == 4);
    CHECK(pMade->Procedure == 23);
    // No package made: nothing to fill.
    CHECK(CopyPackage::CreateDoNothing([]() -> Package* { return nullptr; }, [](Package*) { FAIL("filled nothing"); }) == nullptr);
}

TEST_CASE("A released copy ends only its own do-nothing package", "[CopyPackage]")
{
    constexpr uint8_t cDoNothingType = 27, cSandbox = 18;
    CHECK(CopyPackage::EndsOnRelease(cDoNothingType, 4));
    // A placed do-nothing form is a package of the game's own; so is everything else.
    CHECK_FALSE(CopyPackage::EndsOnRelease(cDoNothingType, 0));
    CHECK_FALSE(CopyPackage::EndsOnRelease(cSandbox, 4));
}

TEST_CASE("This game's own actors take whatever the engine picks", "[CopyPackage]")
{
    CHECK(CopyPackage::Put(false, 0, cCombatOverride) == cCombatOverride);
    CHECK(CopyPackage::Put(false, cFollow, cCombatOverride) == cCombatOverride);
}

TEST_CASE("An owner's empty package does not expose a copy's stale extra package", "[CopyPackage]")
{
    CHECK(CopyPackage::Read(true, 0, cFollow) == 0);
    CHECK(CopyPackage::Read(true, 0, 0) == 0);
    CHECK(CopyPackage::Read(true, cFollow, cFollow) == cFollow);
    CHECK(CopyPackage::Read(false, 0, cFollow) == cFollow);
}

TEST_CASE("A copy's fight starts only through its owner's, this game's own actors start their own", "[CopyCombat]")
{
    CHECK_FALSE(CopyCombat::MayStart(true, false)); // StartCombat, an ally joining: this game deciding
    CHECK(CopyCombat::MayStart(true, true));        // CopyCombatGroups::Hold putting its owner's fight on it
    CHECK(CopyCombat::MayStart(false, false));
}

TEST_CASE("A copy's controller follows its owner's fight", "[CopyCombat]")
{
    using enum CopyCombat::Step;
    // The owner fights: the first copy of its group starts it here, the next ones join it.
    CHECK(CopyCombat::Next(1, false, false, true) == kStart);
    CHECK(CopyCombat::Next(1, false, true, true) == kJoin);
    CHECK(CopyCombat::Next(2, true, true, true) == kNone);
    // No target here yet: nothing to start a group on.
    CHECK(CopyCombat::Next(1, false, false, false) == kNone);
    // The fight ends.
    CHECK(CopyCombat::Next(0, true, true, true) == kStop);
    CHECK(CopyCombat::Next(0, false, false, false) == kNone);
}

TEST_CASE("A copy's group holds its owner's group's targets", "[CopyCombat]")
{
    const auto cChanges = CopyCombat::Diff({0x14, 0xFF000822}, {0x14, 0xFF000830, 0});
    CHECK(cChanges.Add == std::vector<uint32_t>{0xFF000830});
    CHECK(cChanges.Remove == std::vector<uint32_t>{0xFF000822});
    CHECK(CopyCombat::Diff({0x14}, {0x14}).Add.empty());
}

TEST_CASE("A copy searches while its owner searches", "[CopyCombat]")
{
    CHECK(CopyCombat::SearchState(2) != 0); // GetCombatState (38556) reads 2
    CHECK(CopyCombat::SearchState(1) == 0);
    CHECK(CopyCombat::SearchState(0) == 0);
}
