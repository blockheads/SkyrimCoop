#include <catch2/catch.hpp>
#include <Games/Animation/ReplayState.h>

TEST_CASE("Historical animation flags preserve the copy's current weapon transition", "[copies][replay-state]")
{
    CHECK((ReplayState::Flags2(5u << 5, 0) & ReplayState::kWeaponMask) == 0);
    CHECK((ReplayState::Flags2(0, 3u << 5) & ReplayState::kWeaponMask) == 3u << 5);
    CHECK((ReplayState::Flags2(3u << 5, 2u << 5) & ReplayState::kWeaponMask) == 2u << 5);
    CHECK((ReplayState::Flags2(0x64010A8, 0) & ~ReplayState::kWeaponMask) == 0x6401008);
}
