// System quests (docs/design/system-quests.md): which quests are one per player, where a call into one runs, and what
// its instance's natives do.
#include "Routes.h"

#include <catch2/catch.hpp>

using namespace Routes;

namespace
{
constexpr uint32_t kGuest = 0xFF001234;     // a guest's character, as the host's GuestScripts tags it
constexpr uint32_t kVampireQuest = 0xEAFD5; // PlayerVampireQuest
constexpr uint32_t kFalion = 0x5C625;       // VC01
} // namespace

SCENARIO("A quest is a system quest once this player's own script calls into it", "[party][system-quests]")
{
    THEN("the disease (an effect on the player) calling PlayerVampireQuest.VampireChange teaches it")
    CHECK(LearnsSystemQuest(kVampireQuest, 0, true, false, false));
    THEN("so does another system quest's call into it")
    CHECK(LearnsSystemQuest(kVampireQuest, 0x1234, false, true, false));
    THEN("a story quest's call does not (Falion's cure)")
    CHECK_FALSE(LearnsSystemQuest(kVampireQuest, kFalion, false, false, false));
    THEN("nor the quest's own calls, nor a call into a guest's own quest (its beast form)")
    {
        CHECK_FALSE(LearnsSystemQuest(kVampireQuest, kVampireQuest, true, true, false));
        CHECK_FALSE(LearnsSystemQuest(kVampireQuest, 0, true, false, true));
    }
    THEN("a call into what is not a quest teaches nothing") CHECK_FALSE(LearnsSystemQuest(0, 0, true, false, false));
}

SCENARIO("A call into a system quest runs on the instance of the player behind it", "[party][system-quests]")
{
    GIVEN("a player's own action")
    {
        THEN("the guest's disease runs on the guest's game")
        CHECK(RouteOfSystemCall(kThisPlayerFormId, false, false, false, false) == Routing{});
        THEN("the host's own runs on the host's") CHECK(RouteOfSystemCall(kThisPlayerFormId, false, false, true, false) == Routing{});
        THEN("a guest's feed on the host's NPC goes to that guest's game, not run here")
        CHECK(RouteOfSystemCall(kGuest, false, true, true, false) == Routing{.To = Destination::kCauseGame, .Original = false});
        THEN("as does one returning a value: its game answers")
        CHECK(RouteOfSystemCall(kGuest, false, true, true, true) == Routing{.To = Destination::kCauseGame, .Original = false});
    }
    GIVEN("the story")
    {
        THEN("with no player's action (Falion's stage), every game runs it on its own player")
        CHECK(RouteOfSystemCall(0, false, false, true, false) == Routing{.To = Destination::kEveryGame, .Mark = true});
        THEN("a stage a guest's action set: every game, the host on its own player, not the guest's character")
        CHECK(RouteOfSystemCall(kGuest, true, true, true, false) ==
              Routing{.To = Destination::kEveryGame, .Original = false, .ThisPlayer = true, .Mark = true, .MarkFor = kGuest});
        THEN("one returning a value is answered by the host's instance")
        CHECK(RouteOfSystemCall(0, false, false, true, true) == Routing{});
        THEN("on a guest it runs here: the host relays the story")
        CHECK(RouteOfSystemCall(0, false, false, false, false) == Routing{});
    }
    THEN("another game's relayed call runs here only") CHECK(RouteOfSystemCall(MirroredTag(1), false, false, false, false) == Routing{});
}

SCENARIO("A system quest's natives act for this game's own player", "[party][system-quests]")
{
    GIVEN("the host's instance, with no player's action behind it (its hunger's timer)")
    {
        THEN("a progression on the player is this player's, not the party's")
        CHECK(RouteOf({.Reach = kStory, .Target = Target::kThisPlayer, .TargetId = kThisPlayerFormId, .Hosting = true, .InSystemQuest = true}) ==
              Routing{});
        THEN("so are the controls and the screen")
        CHECK(RouteOf({.Reach = {Call::kControls}, .Hosting = true, .InSystemQuest = true}) == Routing{});
        THEN("a question about the player is this player's")
        CHECK(RouteOf({.Reach = {Call::kQuestion}, .Hosting = true, .PlayerInCall = true, .InSystemQuest = true}) == Routing{});
    }
    GIVEN("a guest's instance")
    {
        THEN("a change to the world goes to the host (VampireChange's marker, VC01's stage)")
        CHECK(RouteOf({.Reach = {Call::kWorld}, .Cause = kThisPlayerFormId, .Guest = true, .InSystemQuest = true}) ==
              Routing{.To = Destination::kHost, .Original = false});
        THEN("its own quest's and globals' changes stay here (PlayerIsVampire)")
        CHECK(RouteOf({.Reach = {Call::kWorld}, .Cause = kThisPlayerFormId, .Guest = true, .InSystemQuest = true, .SystemObject = true}) ==
              Routing{});
        THEN("progression on its player stays here")
        CHECK(RouteOf({.Reach = kStory, .Target = Target::kThisPlayer, .TargetId = kThisPlayerFormId, .Cause = kThisPlayerFormId,
                       .Guest = true, .InSystemQuest = true}) == Routing{});
    }
    THEN("any script's change to a system quest's global on a guest stays its own")
    CHECK(RouteOf({.Reach = {Call::kWorld}, .Cause = kThisPlayerFormId, .Guest = true, .SystemObject = true}) == Routing{});
}

TEST_CASE("Only a quest's own script's functions are its instance's", "[party][system-quests]")
{
    CHECK(IsQuestScriptFunction("PlayerVampireQuestScript"));
    CHECK(IsQuestScriptFunction("_Frost_PlayerScript"));
    // Quest.SetStage and Quest.GetStage are script functions over the natives: VC01.SetStage(25) is the story's world.
    CHECK_FALSE(IsQuestScriptFunction("Quest"));
    CHECK_FALSE(IsQuestScriptFunction("quest"));
    CHECK_FALSE(IsQuestScriptFunction("ReferenceAlias"));
    CHECK_FALSE(IsQuestScriptFunction("Form"));
    CHECK_FALSE(IsQuestScriptFunction(""));
}
