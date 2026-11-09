// HookUpdateDetectionState's rule (docs/re/systems/stealth.md): another player's copy never sees this game's player,
// and its pass is installed unseen rather than skipped.
#include <catch2/catch.hpp>

#include <Games/Skyrim/DetectionRules.h>

using namespace DetectionRules;

namespace
{
constexpr Pass kSeen{.Level = 50, .LineOfSight = true, .Seen = true, .Unk16 = 1};
}

TEST_CASE("Another player's copy judging this game's player installs an unseen pass", "[DetectionRules]")
{
    const Pass cPass = Installed(true, true, kSeen);
    CHECK(cPass.Level == kUnseen);
    CHECK_FALSE(cPass.LineOfSight);
    CHECK_FALSE(cPass.Seen);
    CHECK(cPass.Unk16 == 0);
}

TEST_CASE("Every other witness keeps the pass it computed", "[DetectionRules]")
{
    CHECK(Installed(false, true, kSeen) == kSeen);  // an NPC judging this game's player
    CHECK(Installed(true, false, kSeen) == kSeen);  // a player's copy judging an NPC
    CHECK(Installed(false, false, kSeen) == kSeen); // an NPC judging an NPC
}
