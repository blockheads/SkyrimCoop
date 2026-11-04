#include <catch2/catch.hpp>
#include <Games/Animation/ReplayState.h>

TEST_CASE("Historical animation flags preserve the copy's current weapon transition", "[copies][replay-state]")
{
    CHECK((ReplayState::Flags2(5u << 5, 0) & ReplayState::kWeaponMask) == 0);
    CHECK((ReplayState::Flags2(0, 3u << 5) & ReplayState::kWeaponMask) == 3u << 5);
    CHECK((ReplayState::Flags2(3u << 5, 2u << 5) & ReplayState::kWeaponMask) == 2u << 5);
    CHECK((ReplayState::Flags2(0x64010A8, 0) & ~ReplayState::kWeaponMask) == 0x6401008);
}

TEST_CASE("A replayed pair plays only on a free partner; two games' pairs on the same two clash", "[copies][replay-state][kill-move]")
{
    using ReplayState::PairReplay;
    CHECK(ReplayState::ReplayPair(true, false, false, false) == PairReplay::kPlay);
    CHECK(ReplayState::ReplayPair(false, false, false, false) == PairReplay::kPlay);
    CHECK(ReplayState::ReplayPair(true, true, true, true) == PairReplay::kSkip);   // the pair's other half came back
    CHECK(ReplayState::ReplayPair(true, true, false, false) == PairReplay::kSkip); // paired with someone else
    CHECK(ReplayState::ReplayPair(true, true, false, true) == PairReplay::kClash);
}
