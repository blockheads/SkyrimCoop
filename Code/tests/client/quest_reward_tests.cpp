// Quest rewards (docs/design/quest-rewards.md): the reward natives are progression calls, run on every game as the
// engine's own native there (PartyRules::ProgressOf, RouteOf).
#include <Papyrus/PartyRules.h>
#include <catch2/catch.hpp>

using namespace PartyRules;

namespace
{
constexpr uint32_t kGuest = 0xFF001234; // a guest's character, as the host's GuestScripts tags it

Route RouteFor(std::string_view aClass, std::string_view aFunction, Target aTarget, uint32_t aCause, bool aStory = false,
               bool aBody = false, bool aHosting = true)
{
    const auto cProgress = ProgressOf(aClass, aFunction);
    REQUIRE(cProgress);
    return RouteOf(*cProgress, aTarget, aCause, aStory, aBody, aHosting);
}
} // namespace

TEST_CASE("Every reward native is a progression call, in any case", "[rewards]")
{
    for (const auto cFunction : {"AdvanceSkill", "IncrementSkill", "IncrementSkillBy", "AddPerkPoints", "advanceskill"})
        CHECK(ProgressOf("Game", cFunction));
    for (const auto cFunction : {"AddPerk", "RemovePerk", "ModActorValue"})
        CHECK(ProgressOf("Actor", cFunction));
}

TEST_CASE("Story skill use is shared; persuasion, effects and another game's call remain personal", "[rewards]")
{
    CHECK(RouteFor("Game", "AdvanceSkill", Target::kThisPlayer, kGuest, true).Relay);
    CHECK(RouteFor("Game", "AdvanceSkill", Target::kThisPlayer, 0).Relay);
    CHECK_FALSE(RouteFor("Game", "AdvanceSkill", Target::kThisPlayer, kGuest).Relay); // a persuade's: its picker's
    // Amren's dialogue fragment (before its stage) and the Oghma's book: explicit grants, whoever's action.
    CHECK(RouteFor("Game", "IncrementSkillBy", Target::kThisPlayer, kGuest).Relay);
    CHECK(RouteFor("Game", "AddPerkPoints", Target::kThisPlayer, kGuest).Relay);
    CHECK_FALSE(RouteFor("Game", "IncrementSkill", Target::kThisPlayer, kGuest, false, true).Relay);
    CHECK_FALSE(RouteFor("Game", "IncrementSkill", Target::kThisPlayer, MirroredTag(1)).Relay);
    CHECK_FALSE(RouteFor("Game", "IncrementSkill", Target::kThisPlayer, 0, false, false, false).Relay);
}

TEST_CASE("A perk or a permanent value on a guest's character reaches its own player, not the character", "[rewards]")
{
    for (const auto cFunction : {"AddPerk", "RemovePerk", "ModActorValue"})
    {
        const Route cRoute = RouteFor("Actor", cFunction, Target::kGuestCharacter, kGuest);
        CHECK(cRoute.Relay);
        CHECK(cRoute.ThisPlayer);
        CHECK_FALSE(cRoute.Original);
    }
    const Route cOwn = RouteFor("Actor", "AddPerk", Target::kThisPlayer, 0);
    CHECK(cOwn.Original);
    CHECK_FALSE(cOwn.ThisPlayer);
    CHECK_FALSE(RouteFor("Actor", "ModActorValue", Target::kNone, 0).Relay); // an NPC's
}

TEST_CASE("Items, gold and temporary values are not rewards", "[rewards]")
{
    CHECK_FALSE(ProgressOf("ObjectReference", "AddItem"));
    CHECK_FALSE(ProgressOf("ObjectReference", "RemoveItem"));
    CHECK_FALSE(ProgressOf("Actor", "DamageActorValue"));
    CHECK_FALSE(ProgressOf("Actor", "RestoreActorValue"));
    CHECK_FALSE(ProgressOf("Game", "ShowTrainingMenu"));
}
