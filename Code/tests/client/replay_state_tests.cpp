#include <catch2/catch.hpp>
#include <Games/Animation/ReplayState.h>

TEST_CASE("Historical animation flags preserve the copy's current weapon transition", "[copies][replay-state]")
{
    CHECK((ReplayState::Flags2(5u << 5, 0) & ReplayState::kWeaponMask) == 0);
    CHECK((ReplayState::Flags2(0, 3u << 5) & ReplayState::kWeaponMask) == 3u << 5);
    CHECK((ReplayState::Flags2(3u << 5, 2u << 5) & ReplayState::kWeaponMask) == 2u << 5);
    CHECK((ReplayState::Flags2(0x64010A8, 0) & ~ReplayState::kWeaponMask) == 0x6401008);
}

TEST_CASE("A replayed pair plays on a free partner; crossed, the first swing wins on both games", "[copies][replay-state][kill-move]")
{
    using namespace ReplayState;
    const auto cSettle = [](std::string_view aEvent, uint32_t aCopy, bool aInKillMove, std::string_view aPairEvent, uint32_t aTarget, uint64_t aTick, uint64_t aPairTick, uint32_t aSession = 2, uint32_t aPairSession = 1) {
        return SettlePairedReplay(PairedReplay{aEvent, aCopy, aInKillMove, aPairEvent, aTarget, aTick, aPairTick, aSession, aPairSession});
    };
    CHECK(cSettle("pa_A", 5, false, "", 0, 10, 0) == PairOutcome::kPlay);
    CHECK(cSettle("A", 5, true, "A", 5, 10, 10) == PairOutcome::kSkip);     // the pair's other half
    CHECK(cSettle("pa_A", 5, true, "B", 6, 10, 20) == PairOutcome::kSkip);  // paired with someone else
    CHECK(cSettle("pa_A", 5, true, "B", 5, 10, 20) == PairOutcome::kYield); // it swung first
    CHECK(cSettle("pa_A", 5, true, "B", 5, 20, 10) == PairOutcome::kSkip);  // this game swung first
    // The same two swings seen from each game: exactly one yields, a tie included.
    CHECK(cSettle("pa_A", 5, true, "B", 5, 10, 10, 2, 1) == PairOutcome::kSkip);
    CHECK(cSettle("pa_B", 7, true, "A", 7, 10, 10, 1, 2) == PairOutcome::kYield);
}

TEST_CASE("A blocked mark refuses an owner's pair, never a replayed one", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::PairMarkClear(true, false));
    CHECK_FALSE(ReplayState::PairMarkClear(false, false));
    CHECK(ReplayState::PairMarkClear(false, true));
}

TEST_CASE("A copy in a pair takes no plain action of its owner", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::HeldByPair(true, false));
    CHECK_FALSE(ReplayState::HeldByPair(true, true));
    CHECK_FALSE(ReplayState::HeldByPair(false, false));
}

TEST_CASE("A yielded clip's blow is dropped until its player attacks anew", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::CancelledBlow(true));
    CHECK_FALSE(ReplayState::CancelledBlow(false));
    CHECK(ReplayState::StartsAttack("attackStart"));
    CHECK(ReplayState::StartsAttack("attackPowerStartLeft"));
    CHECK(ReplayState::StartsAttack("pa_KillMoveI"));
    CHECK_FALSE(ReplayState::StartsAttack("KillMoveI")); // the victim's half
    CHECK_FALSE(ReplayState::StartsAttack("moveStop"));
}

TEST_CASE("pairedStop is sent again only while the cancelled clip plays", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::ResendPairStop(1, false, true));
    CHECK_FALSE(ReplayState::ResendPairStop(0, false, true));
    CHECK_FALSE(ReplayState::ResendPairStop(1, true, true)); // a new kill move
    CHECK_FALSE(ReplayState::ResendPairStop(1, false, false));
}

TEST_CASE("Contact kills a kill move's victim only when both halves are this game's", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::ContactKills(false));
    CHECK_FALSE(ReplayState::ContactKills(true));
}

TEST_CASE("A copy holds its pose until its owner's death, or its time", "[copies][replay-state][kill-move]")
{
    CHECK(ReplayState::PoseHeld(100, 200, false));
    CHECK_FALSE(ReplayState::PoseHeld(200, 200, false));
    CHECK_FALSE(ReplayState::PoseHeld(100, 200, true));
    CHECK_FALSE(ReplayState::PoseHeld(100, 0, false));
}

TEST_CASE("A copy getting off its mount is on its own body until its sit state leaves wants-to-stand", "[replay][graph-stream]")
{
    // Nothing else marks it: a copy's exit sets no GETTING_ON_OFF_MOUNT (Actor::Dismount 37906 never runs for it).
    CHECK(ReplayState::CopyOwnBody(false, false, false, false, ReplayState::kSitWantsToStand));
    CHECK_FALSE(ReplayState::CopyOwnBody(false, false, false, false, 0));
    CHECK_FALSE(ReplayState::CopyOwnBody(false, false, false, false, 3)); // sitting: riding, on its stream
    CHECK(ReplayState::CopyOwnBody(false, false, false, true, 2));        // getting on: the engine's flag
    CHECK(ReplayState::CopyOwnBody(true, false, false, false, 0));
    CHECK(ReplayState::CopyOwnBody(false, true, false, false, 0));
    CHECK(ReplayState::CopyOwnBody(false, false, true, false, 0));
}
