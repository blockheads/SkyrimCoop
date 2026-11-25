// HookInitFromPackage's rule (actor-ai-and-copies.md): a copy runs its owner's package, never one its own game picks.
#include <catch2/catch.hpp>

#include <algorithm>
#include <vector>

#include <Games/Skyrim/CopyCombat.h>
#include <Games/Skyrim/CopyPackage.h>

namespace
{
bool AnswersOwner(uint32_t aId, uint32_t aReturnOffset)
{
    return std::ranges::any_of(CopyCombat::cOwnerAnswerSites,
                               [&](const auto& acSite) { return acSite.Id == aId && acSite.ReturnOffset == aReturnOffset; });
}

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

TEST_CASE("A copy takes no combat controller, this game's own actors do", "[CopyCombat]")
{
    CHECK_FALSE(CopyCombat::MayStart(true));
    CHECK(CopyCombat::MayStart(false));
}

TEST_CASE("A copy's regen rate asks for its owner's combat state", "[CopyCombat]")
{
    // Actor::GetActorValueRegenRate (38460): the essential health bonus and the combat multiplier.
    CHECK(AnswersOwner(38460, 0xC5));
    CHECK(AnswersOwner(38460, 0x1FC));
    // The conditions still do; another engine call into 38460 does not.
    CHECK(AnswersOwner(21576, 0x38));
    CHECK(AnswersOwner(21740, 0x28));
    CHECK_FALSE(AnswersOwner(38460, 0x10));
    // Actor::EngagingInCombat (38563, its call returns at +0x12) uses the combat controller after a yes.
    CHECK_FALSE(AnswersOwner(38563, 0x12));
}

TEST_CASE("A copy is on the compass when its owner's combat group targets this game's player", "[CopyCombat]")
{
    constexpr uint32_t cPlayer = 0x14;
    const std::vector<uint32_t> cTargets{0xFF000822, cPlayer};
    CHECK(CopyCombat::OnCompass(true, cTargets, cPlayer));
    CHECK_FALSE(CopyCombat::OnCompass(true, std::vector<uint32_t>{0xFF000822}, cPlayer)); // only another player
    CHECK_FALSE(CopyCombat::OnCompass(true, std::vector<uint32_t>{}, cPlayer));
    // One of this game's own actors is in a group: 41240 lists it itself.
    CHECK_FALSE(CopyCombat::OnCompass(false, cTargets, cPlayer));
}
