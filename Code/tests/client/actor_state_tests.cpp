#include <catch2/catch.hpp>
#include <Games/Animation/ActorStateRules.h>

using namespace ActorStateRules;

TEST_CASE("Each bit of a copy's actor state has one writer: the record, its owner's flags, or its own body", "[actor-state]")
{
    // The record's bits and the copy's own (knock, weapon transition) never overlap.
    CHECK((kPartBits1 & kKnockMask) == 0);
    CHECK((kPartBits2 & kWeaponMask) == 0);

    // Its owner's flags (an action on its stream, a sync point) give all but those: an attack's and a sit's start,
    // recoil, and every bit not read yet (movement, swimming, fly state, staggered) as before the record.
    constexpr uint32_t cSwimming = 1u << 10, cFlying = 2u << 18, cStaggered = 1u << 13;
    CHECK(FromSnapshot1(kSneaking | 2u << 28 | 3u << 14 | cSwimming | cFlying | 1u << 25, 2u << 25) == (2u << 28 | 3u << 14 | cSwimming | cFlying | 2u << 25));
    CHECK(FromSnapshot1(0, kSneaking | 3u << 14 | cSwimming) == kSneaking); // sneak stays the record's
    CHECK(FromSnapshot2(kWantBlocking | 3u << 5 | kRecoilMask | cStaggered, 2u << 5) == (kRecoilMask | cStaggered | 2u << 5));
}

TEST_CASE("A copy takes its owner's sneak, block and bleedout from the record, never a death", "[actor-state]")
{
    constexpr uint32_t cBleedout = 8u << 21;
    constexpr uint32_t cDying = uint32_t(kLifeDying) << 21;
    constexpr uint32_t cDead = uint32_t(kLifeDead) << 21;
    CHECK(FromPart1(kSneaking | cBleedout, 3u << 28) == (kSneaking | cBleedout | 3u << 28));
    CHECK(FromPart1(0, kSneaking | cBleedout) == 0); // up again
    CHECK(FromPart1(cDying | kSneaking, 0) == kSneaking); // its death is its stream's
    CHECK(FromPart1(0, cDead) == cDead);                  // a dead copy stays dead
    CHECK(FromPart2(kWantBlocking, kRecoilMask) == (kWantBlocking | kRecoilMask));
}

TEST_CASE("A copy's part waits for its stream to reach its owner's frame", "[actor-state]")
{
    CHECK_FALSE(Due(true, 99, 100));
    CHECK(Due(true, 100, 100));
    CHECK(Due(false, 0, 100)); // off its stream (no stream, or held for a pair): at once
}

TEST_CASE("A copy enters its owner's kill move from the record, and leaves one its owner left once no pair plays", "[actor-state][kill-move]")
{
    CHECK(EntersKillMove(true, false, false));
    CHECK_FALSE(EntersKillMove(true, true, false));  // in it already (its own action, off its stream)
    CHECK_FALSE(EntersKillMove(true, false, true));

    CHECK_FALSE(LeavesKillMove(false, true, true, false)); // its pair plays: its own KillMoveEnd (42871) ends it
    CHECK(LeavesKillMove(false, true, false, false));      // made mid-kill-move, its end before its sync point
    CHECK_FALSE(LeavesKillMove(true, true, false, false));
    CHECK_FALSE(LeavesKillMove(false, true, false, true));
}

TEST_CASE("Drawn hands: the owner's settled state, put right on a copy once it is not mid-draw or on its stream", "[actor-state][weapon-draw]")
{
    CHECK(SettledHands(WeaponState::Drawn, false) == true);
    CHECK(SettledHands(WeaponState::Sheathed, false) == false);
    CHECK_FALSE(SettledHands(WeaponState::Drawing, false).has_value()); // its copy's own draw handlers are on their way
    // A dying owner's hands drop with its death, which its copies play later: sent now, a copy would sheathe first.
    CHECK_FALSE(SettledHands(WeaponState::Sheathed, true).has_value());
    CHECK(PutsHandsRight(true, WeaponState::Sheathed, false));
    CHECK(PutsHandsRight(true, WeaponState::WantToDraw, false)); // a copy's refused request stays pending for good
    CHECK_FALSE(PutsHandsRight(true, WeaponState::Drawing, false));
    // On its stream its own weaponDraw (42824) never comes: a draw left from before its stream is ended now.
    CHECK(PutsHandsRight(true, WeaponState::Drawing, true));
    CHECK(PutsHandsRight(false, WeaponState::Sheathing, false) == false);
    CHECK(PutsHandsRight(true, WeaponState::Sheathing, true));
    CHECK_FALSE(HandsSettled(true, WeaponState::Drawing));    // looked at again once its animation ends
    CHECK_FALSE(HandsSettled(true, WeaponState::WantToDraw)); // a refused draw (37279) is tried again
    CHECK(HandsSettled(true, WeaponState::Drawn));
    CHECK(HandsSettled(false, WeaponState::Sheathed));
    CHECK_FALSE(PutsHandsRight(true, WeaponState::Drawn, true));
    CHECK(PutsHandsRight(false, WeaponState::WantToSheathe, false));
}

TEST_CASE("A copy's noise is its owner's: held while its owner's lasts, never its own engine's", "[actor-state][stealth]")
{
    CHECK(MakesOwnNoise(false));
    CHECK_FALSE(MakesOwnNoise(true)); // a replayed swing or cast on the copy (37711) is not its
    CHECK(CopyNoiseTimer(100) == kHeldNoise);
    CHECK(CopyNoiseTimer(100) - 3600.f > 0.f); // an hour of its own countdown leaves it
    CHECK(CopyNoiseTimer(0) == 0.f);           // its owner's 0 ends it
    CHECK(HoldsOwnersNoise(CopyNoiseTimer(100) - 60.f)); // a minute of its own countdown: still held, ended on release
    CHECK_FALSE(HoldsOwnersNoise(5.f));                  // its own engine's noise runs out by itself
}
