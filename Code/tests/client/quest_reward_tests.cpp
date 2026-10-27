// Quest rewards (docs/design/quest-rewards.md): the reward natives are progression calls, run on every game as the
// engine's own native there (PartyRules::RouteOf).
#include "Routes.h"

#include <catch2/catch.hpp>

using namespace Routes;

namespace
{
constexpr uint32_t kGuest = 0xFF001234; // a guest's character, as the host's GuestScripts tags it
} // namespace

TEST_CASE("Story skill use is shared; persuasion, effects and another game's call remain personal", "[rewards]")
{
    CHECK(Relays(ProgressRoute(kStory, Target::kThisPlayer, kGuest, true)));  // AdvanceSkill from a stage
    CHECK(Relays(ProgressRoute(kStory, Target::kThisPlayer, 0)));
    CHECK_FALSE(Relays(ProgressRoute(kStory, Target::kThisPlayer, kGuest))); // a persuade's: its picker's
    // Amren's dialogue fragment (before its stage) and the Oghma's book: explicit grants, whoever's action.
    CHECK(Relays(ProgressRoute(kReward, Target::kThisPlayer, kGuest)));
    CHECK_FALSE(Relays(ProgressRoute(kReward, Target::kThisPlayer, kGuest, false, true)));
    CHECK_FALSE(Relays(ProgressRoute(kReward, Target::kThisPlayer, MirroredTag(1))));
    CHECK_FALSE(Relays(ProgressRoute(kReward, Target::kThisPlayer, 0, false, false, false)));
}

TEST_CASE("A perk or a permanent value on a guest's character reaches its own player, not the character", "[rewards]")
{
    const Routing cRoute = ProgressRoute(kReward, Target::kGuestCharacter, kGuest);
    CHECK(Relays(cRoute));
    CHECK(cRoute.ThisPlayer);
    CHECK_FALSE(cRoute.Original);
    CHECK(cRoute.MarkFor == kGuest);
    const Routing cOwn = ProgressRoute(kReward, Target::kThisPlayer, 0);
    CHECK(cOwn.Original);
    CHECK_FALSE(cOwn.ThisPlayer);
    CHECK_FALSE(Relays(ProgressRoute(kReward, Target::kNone, 0))); // an NPC's
}
