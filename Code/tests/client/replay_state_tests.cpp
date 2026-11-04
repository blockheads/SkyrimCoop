#include <catch2/catch.hpp>
#include <Games/Animation/ReplayState.h>

TEST_CASE("Historical animation flags preserve the copy's current weapon transition", "[copies][replay-state]")
{
    CHECK((ReplayState::Flags2(5u << 5, 0) & ReplayState::kWeaponMask) == 0);
    CHECK((ReplayState::Flags2(0, 3u << 5) & ReplayState::kWeaponMask) == 3u << 5);
    CHECK((ReplayState::Flags2(3u << 5, 2u << 5) & ReplayState::kWeaponMask) == 2u << 5);
    CHECK((ReplayState::Flags2(0x64010A8, 0) & ~ReplayState::kWeaponMask) == 0x6401008);
}

TEST_CASE("A replayed pair plays only on a free partner; two games' pairs on the same two end", "[copies][replay-state][kill-move]")
{
    using ReplayState::PairedReplay;
    int ended = 0;
    const auto cEnd = [&](bool) { ++ended; };
    CHECK_FALSE(ReplayState::SettlePairedReplay(PairedReplay{"pa_A", 5, true, false, "", 0}, cEnd));
    CHECK_FALSE(ReplayState::SettlePairedReplay(PairedReplay{"pa_A", 5, false, true, "B", 5}, cEnd));
    CHECK(ended == 0);
    CHECK(ReplayState::SettlePairedReplay(PairedReplay{"A", 5, true, true, "A", 5}, cEnd)); // the pair's other half
    CHECK(ReplayState::SettlePairedReplay(PairedReplay{"pa_A", 5, true, true, "B", 6}, cEnd)); // paired with someone else
    CHECK(ended == 0);
    CHECK(ReplayState::SettlePairedReplay(PairedReplay{"pa_A", 5, true, true, "B", 5}, cEnd));
    CHECK(ended == 2);
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
