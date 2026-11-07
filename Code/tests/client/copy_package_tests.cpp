// HookInitFromPackage's rule (actor-ai-and-copies.md): a copy runs its owner's package, never one its own game picks.
#include <catch2/catch.hpp>

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
