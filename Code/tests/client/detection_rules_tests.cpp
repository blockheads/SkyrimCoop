// HookUpdateDetectionState's rule (docs/re/systems/stealth.md): a copy never sees this game's player, and its pass is
// installed unseen rather than skipped. The eye's rules: what it shows, and when the host sends a guest its reading.
#include <catch2/catch.hpp>

#include <Games/Skyrim/DetectionRules.h>

using namespace DetectionRules;

namespace
{
constexpr Pass kSeen{.Level = 50, .LineOfSight = true, .Seen = true, .Unk16 = 1};
}

TEST_CASE("A copy judging this game's player installs an unseen pass", "[DetectionRules]")
{
    const Pass cPass = Installed(true, true, kSeen);
    CHECK(cPass.Level == kUnseen);
    CHECK_FALSE(cPass.LineOfSight);
    CHECK_FALSE(cPass.Seen);
    CHECK(cPass.Unk16 == 0);
}

TEST_CASE("Every other witness keeps the pass it computed", "[DetectionRules]")
{
    CHECK(Installed(false, true, kSeen) == kSeen);  // this game's NPC judging this game's player
    CHECK(Installed(true, false, kSeen) == kSeen);  // a copy judging another copy (the host's NPC of a guest)
    CHECK(Installed(false, false, kSeen) == kSeen); // an NPC judging an NPC
}

TEST_CASE("Head tracking asks an actor's own detection, never a copy's", "[detection-rules]")
{
    // 38009 drops a look at this game's player its tracker does not detect; a copy looks where its owner does.
    CHECK(DetectionRules::HeadTrackAsksDetection(false));
    CHECK_FALSE(DetectionRules::HeadTrackAsksDetection(true));
}

TEST_CASE("The eye shows 100 for anyone detecting, in combat 100 minus the stealth points", "[DetectionRules][stealth]")
{
    CHECK(EyeShows(false, 31, 100.f) == 100);
    CHECK(EyeShows(false, -7, 100.f) == 0);
    CHECK(EyeShows(false, kUnseen, 100.f) == 0);
    CHECK(EyeShows(true, 146, 0.f) == 100);    // a fight: the level aside
    CHECK(EyeShows(true, 146, 79.7f) == 21);
    CHECK(EyeShows(true, 146, 100.f) == 0);    // no group fighting it (46877's default)
}

TEST_CASE("The host sends a guest its eye again only when it could read otherwise", "[DetectionRules][stealth]")
{
    const EyeReading cSeen{.Level = 31, .Seeing = 1, .Stealth = 100.f};
    CHECK_FALSE(EyeChanged(cSeen, cSeen));
    CHECK_FALSE(EyeChanged(cSeen, {.Level = 34, .Seeing = 1, .Stealth = 100.f}));     // within a step
    CHECK(EyeChanged(cSeen, {.Level = 36, .Seeing = 1, .Stealth = 100.f}));           // a step
    CHECK(EyeChanged({.Level = 2, .Seeing = 1}, {.Level = -1, .Seeing = 1}));          // seen no more, however near
    CHECK(EyeChanged(cSeen, {.Level = 31, .Seeing = 0, .Stealth = 100.f}));           // nobody seeing (sneak XP)
    CHECK_FALSE(EyeChanged(cSeen, {.Level = 31, .Seeing = 3, .Stealth = 100.f}));     // how many: only whether
    CHECK(EyeChanged(cSeen, {.Level = 31, .Seeing = 1, .Stealth = 99.f}));            // the fight's eye moves a point
    CHECK_FALSE(EyeChanged(cSeen, {.Level = 31, .Seeing = 1, .Stealth = 100.4f}));
}
