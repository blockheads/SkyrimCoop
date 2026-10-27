// PartyRules: whose a controls, screen or progression call is, and who holds the controls (docs/design/cutscenes.md,
// docs/design/dragonborn.md).
#include <catch2/catch.hpp>

#include <Papyrus/PartyRules.h>

using namespace PartyRules;

namespace
{
constexpr uint32_t kThisPlayer = 0x14;  // GuestScripts' cause for this game's own player
constexpr uint32_t kGuest = 0xFF000801; // a guest's character, as the host's GuestScripts tags it
const uint32_t kHostsGame = MirroredTag(0);
const uint32_t kGuestsGame = MirroredTag(1);
constexpr uint32_t kCutscene = kMovement | kFighting | kLooking | kSneaking | kMainFour; // Game.DisablePlayerControls()
} // namespace

TEST_CASE("Mirrored tags carry the sending game's player id", "[party]")
{
    CHECK(IsMirrored(MirroredTag(3)));
    CHECK(PlayerOf(MirroredTag(3)) == 3);
    CHECK(!IsMirrored(0));
    CHECK(!IsMirrored(kThisPlayer));
    CHECK(!IsMirrored(kThisGame));
    CHECK(HolderOf(0) == kThisGame);
    CHECK(HolderOf(kThisPlayer) == kThisGame);
    CHECK(HolderOf(MirroredTag(2)) == MirroredTag(2));
}

SCENARIO("A player's moment stays theirs; the story is the party's", "[party]")
{
    GIVEN("a call no player's action is behind (a stage fragment, a quest's timer)")
    THEN("it is the party's") CHECK(Whose(0, false) == Share::kParty);

    GIVEN("this game's player's own action (a lever, fishing, a ride)")
    {
        THEN("it is theirs") CHECK(Whose(kThisPlayer, false) == Share::kPlayer);
        AND_WHEN("a stage that action set runs it")
        THEN("it is the story, the party's") CHECK(Whose(kThisPlayer, true) == Share::kParty);
    }

    GIVEN("a guest's action, run on the host")
    {
        THEN("it is the guest's") CHECK(Whose(kGuest, false) == Share::kPlayer);
        THEN("its story is the party's") CHECK(Whose(kGuest, true) == Share::kParty);
    }

    GIVEN("another game's call, mirrored here")
    THEN("it runs here only, story or not")
    {
        CHECK(Whose(kGuestsGame, false) == Share::kHere);
        CHECK(Whose(kGuestsGame, true) == Share::kHere);
    }
}

SCENARIO("What a reference shows is every game's, whoever's action ran it", "[party][dragonborn]")
{
    THEN("a player's moment's show (the killer's dragon dissolving) is the party's")
    {
        CHECK(ShareOf(Kind::kShow, kGuest, false, 0) == Share::kParty);
        CHECK(ShareOf(Kind::kShow, kThisPlayer, false, 0) == Share::kParty);
        CHECK(ShareOf(Kind::kShow, 0, false, 0) == Share::kParty);
    }
    THEN("another game's, mirrored here, runs here only") CHECK(ShareOf(Kind::kShow, kHostsGame, false, 0) == Share::kHere);
}

SCENARIO("A controls call is shared only if it takes movement", "[party]")
{
    WHEN("a world script takes the movement")
    THEN("the party's") CHECK(ShareOf(Kind::kControls, 0, false, kCutscene) == Share::kParty);

    WHEN("it takes fighting only (a restriction)")
    THEN("this game's alone") CHECK(ShareOf(Kind::kControls, 0, false, kFighting) == Share::kHere);

    WHEN("it takes sneaking and menus")
    THEN("this game's alone") CHECK(ShareOf(Kind::kControls, 0, false, kSneaking | kMainFour) == Share::kHere);

    WHEN("a screen call (a fade) asks for no controls")
    THEN("still the party's") CHECK(ShareOf(Kind::kScreen, 0, false, 0) == Share::kParty);

    WHEN("a player's own moment takes the movement")
    THEN("still theirs") CHECK(ShareOf(Kind::kControls, kThisPlayer, false, kMovement) == Share::kPlayer);

    WHEN("a restriction is a player's own")
    THEN("theirs, not a restriction of this game")
    CHECK(ShareOf(Kind::kControls, kGuest, false, kFighting) == Share::kPlayer);
}

SCENARIO("Overlapping cutscenes each keep their controls", "[party]")
{
    ControlHolds holds;
    GIVEN("this game's cutscene and another game's both take the controls")
    {
        holds.Take(kThisGame, kCutscene);
        holds.Take(kHostsGame, kMovement | kFighting);

        WHEN("this game's ends first")
        {
            const uint32_t cFirst = holds.GiveBack(kThisGame, kCutscene);
            const uint32_t cSecond = holds.GiveBack(kHostsGame, kMovement | kFighting);
            THEN("only what the other does not hold comes back, then the rest")
            {
                CHECK(cFirst == (kLooking | kSneaking | kMainFour));
                CHECK(cSecond == (kMovement | kFighting));
                CHECK(holds.Held() == 0);
            }
        }

        WHEN("the other game's ends first")
        {
            const uint32_t cFirst = holds.GiveBack(kHostsGame, kMovement | kFighting);
            const uint32_t cSecond = holds.GiveBack(kThisGame, kCutscene);
            THEN("nothing it held comes back while this game's holds it, then everything")
            {
                CHECK(cFirst == 0);
                CHECK(cSecond == kCutscene);
                CHECK(holds.Held() == 0);
            }
        }
    }

    GIVEN("a give-back by a holder that never took anything")
    {
        holds.Take(kThisGame, kMovement);
        THEN("what others hold stays") CHECK(holds.GiveBack(kGuestsGame, kMovement | kFighting) == kFighting);
        THEN("no hold is made for it") CHECK(holds.HeldBy(kGuestsGame) == 0);
    }
}

SCENARIO("A leaver's cutscene gives back what it held", "[party]")
{
    ControlHolds holds;
    GIVEN("the guest's cutscene holds the controls on the host's game")
    {
        holds.Take(kGuestsGame, kCutscene);

        WHEN("the guest leaves")
        {
            const auto cReturned = holds.Leave(kGuestsGame);
            THEN("all it held comes back, camera switch, activate and journal too")
            {
                REQUIRE(cReturned.size() == 1);
                CHECK(cReturned[0].Holder == kGuestsGame);
                CHECK(cReturned[0].Free == kCutscene);
                CHECK(cReturned[0].NoneElse);
                CHECK(EnableFor(cReturned[0].Free, cReturned[0].NoneElse) ==
                      EnableControls{true, true, true, true, true, true, true, true});
                CHECK(holds.Held() == 0);
            }
        }

        AND_GIVEN("this game's own cutscene holds the movement")
        {
            holds.Take(kThisGame, kMovement);
            WHEN("the guest leaves")
            {
                const auto cReturned = holds.Leave(kGuestsGame);
                THEN("the movement stays taken, and what ControlMap does not keep stays too")
                {
                    REQUIRE(cReturned.size() == 1);
                    CHECK(cReturned[0].Free == (kCutscene & ~kMovement));
                    CHECK(!cReturned[0].NoneElse);
                    CHECK(EnableFor(cReturned[0].Free, cReturned[0].NoneElse) ==
                          EnableControls{false, true, false, true, true, true, false, false});
                    CHECK(holds.HeldBy(kThisGame) == kMovement);
                }
            }
        }

        AND_GIVEN("another player's cutscene holds the movement")
        {
            holds.Take(kHostsGame, kMovement);
            WHEN("the session ends (every other game's holds)")
            {
                holds.Take(kThisGame, kSneaking);
                const auto cReturned = holds.Leave(std::nullopt);
                THEN("each other game's holds come back, this game's own stay")
                {
                    CHECK(cReturned.size() == 2);
                    for (const auto& cBack : cReturned)
                        CHECK(cBack.Free == (cBack.Held & ~kSneaking));
                    CHECK(holds.Held() == kSneaking);
                }
            }
        }
    }

    GIVEN("a holder that gave everything back already")
    {
        holds.Take(kGuestsGame, kMovement);
        (void)holds.GiveBack(kGuestsGame, kMovement);
        THEN("its leaving gives nothing back") CHECK(holds.Leave(kGuestsGame).empty());
    }
}

SCENARIO("Holds: partial give-backs, several leavers, taking again", "[party]")
{
    ControlHolds holds;

    GIVEN("a cutscene that gives back only part of what it took")
    {
        holds.Take(kThisGame, kCutscene);
        const uint32_t cBack = holds.GiveBack(kThisGame, kMovement);
        THEN("that part comes back, the rest stays held")
        {
            CHECK(cBack == kMovement);
            CHECK(holds.HeldBy(kThisGame) == (kCutscene & ~kMovement));
        }
    }

    GIVEN("two other games' cutscenes holding the movement, and the session ends")
    {
        const uint32_t cThirdGame = MirroredTag(2);
        holds.Take(kHostsGame, kMovement);
        holds.Take(cThirdGame, kMovement | kFighting);
        const auto cReturned = holds.Leave(std::nullopt);
        THEN("each gives back all it held: no one is left")
        {
            REQUIRE(cReturned.size() == 2);
            for (const auto& cBack : cReturned)
            {
                CHECK(cBack.Free == cBack.Held);
                CHECK(cBack.NoneElse);
            }
        }
    }

    GIVEN("two other games holding the movement, and one leaves")
    {
        holds.Take(kHostsGame, kMovement);
        holds.Take(kGuestsGame, kMovement | kFighting);
        const auto cReturned = holds.Leave(kGuestsGame);
        THEN("only its fighting comes back")
        {
            REQUIRE(cReturned.size() == 1);
            CHECK(cReturned[0].Free == kFighting);
            CHECK(!cReturned[0].NoneElse);
        }
    }

    GIVEN("a game that left and joins again")
    {
        holds.Take(kGuestsGame, kMovement);
        (void)holds.Leave(kGuestsGame);
        holds.Take(kGuestsGame, kFighting);
        THEN("its new cutscene holds only what it took now") CHECK(holds.HeldBy(kGuestsGame) == kFighting);
    }

    GIVEN("taking the same controls twice")
    {
        holds.Take(kThisGame, kMovement);
        holds.Take(kThisGame, kMovement);
        THEN("one give-back frees them (ControlMap keeps no count)") CHECK(holds.GiveBack(kThisGame, kMovement) == kMovement);
    }
}

SCENARIO("The party is the Dragonborn: which calls on a player's progression every player takes", "[party][dragonborn]")
{
    const auto cRoute = [](std::string_view aClass, std::string_view aFunction, Target aTarget, uint32_t aCause, bool aStory = false,
                           bool aBody = false, bool aHosting = true) {
        const auto cProgress = ProgressOf(aClass, aFunction);
        REQUIRE(cProgress);
        return RouteOf(*cProgress, aTarget, aCause, aStory, aBody, aHosting);
    };
    GIVEN("a word of power taught or unlocked by a script")
    {
        THEN("it is the party's, whoever's action ran it (a guest's word wall, Paarthurnax)")
        {
            CHECK(cRoute("Game", "TeachWord", Target::kThisPlayer, 0).Relay);
            CHECK(cRoute("Game", "teachword", Target::kThisPlayer, kGuest).Relay);
            CHECK(cRoute("Game", "UnlockWord", Target::kThisPlayer, kThisPlayer).Relay);
        }
        THEN("the host's own player takes it there as the original: nothing more runs here")
        {
            const Route cRun = cRoute("Game", "TeachWord", Target::kThisPlayer, kGuest);
            CHECK(cRun.Original);
            CHECK(!cRun.ThisPlayer);
        }
        THEN("another game's, run here, is not sent again; a guest's game's own scripts are not the story")
        {
            CHECK(!cRoute("Game", "TeachWord", Target::kThisPlayer, kHostsGame).Relay);
            CHECK(!cRoute("Game", "UnlockWord", Target::kThisPlayer, kGuestsGame).Relay);
            CHECK(!cRoute("Game", "TeachWord", Target::kThisPlayer, 0, false, false, false).Relay);
        }
    }
    GIVEN("a dragon's soul absorbed into a guest's character (MQKillDragonScript: AbsorbActor.ModActorValue)")
    {
        const Route cRun = cRoute("Actor", "ModActorValue", Target::kGuestCharacter, kGuest, true);
        THEN("every game's own player gains it; the character itself not: its owner's game supplies its values")
        {
            CHECK(cRun.Relay);
            CHECK(cRun.ThisPlayer);
            CHECK(!cRun.Original);
        }
    }
    GIVEN("a faction or a spell on a player")
    {
        THEN("the story's is the party's: no player behind it, or a stage a player's action set")
        {
            CHECK(cRoute("Actor", "SetFactionRank", Target::kThisPlayer, 0).Relay);
            CHECK(cRoute("Actor", "AddSpell", Target::kGuestCharacter, kGuest, true).Relay);
        }
        THEN("on a guest's character the original runs too: the host's conditions read it there")
        {
            const Route cRun = cRoute("Actor", "AddToFaction", Target::kGuestCharacter, kGuest, true);
            CHECK(cRun.Original);
            CHECK(cRun.ThisPlayer);
        }
        THEN("a player's own moment stays theirs: its own action (a lever), or a body's script (its beast form's quest)")
        {
            CHECK(!cRoute("Actor", "AddToFaction", Target::kGuestCharacter, kGuest).Relay);
            CHECK(!cRoute("Actor", "SetFactionRank", Target::kThisPlayer, kThisPlayer, true, true).Relay);
            CHECK(!cRoute("Actor", "RemoveSpell", Target::kThisPlayer, 0, false, true).Relay);
        }
        THEN("on an NPC it is no player's") CHECK(!cRoute("Actor", "AddToFaction", Target::kNone, 0).Relay);
    }
    GIVEN("what is not progression")
    {
        THEN("a body's values, items and reads are no party call")
        {
            CHECK(!ProgressOf("Actor", "DamageActorValue"));
            CHECK(!ProgressOf("Actor", "ForceActorValue"));
            CHECK(!ProgressOf("ObjectReference", "AddItem"));
            CHECK(!ProgressOf("Actor", "GetFactionRank"));
            CHECK(!ProgressOf("Game", "ShowTrainingMenu"));
        }
    }
    GIVEN("the call a guest's character took, as the host's player takes it")
    {
        LocalCall absorb{.Class = "Actor", .Function = "ModActorValue", .Self = LocalForm{kGuest}, .Arguments = {std::string("DragonSouls"), 1.f}};
        MarkEachPlayer(absorb, kGuest);
        THEN("its player is this game's own") CHECK(std::get<LocalForm>(OnThisPlayer(absorb).Self) == LocalForm{kThisPlayer});
    }
}

SCENARIO("In the story's party call, the player is each game's own", "[party][dragonborn]")
{
    // MQKillDragonScript.DeathSequence for a guest's kill: DragonAbsorbEffect.Play(dragon, 8, AbsorbActor).
    LocalCall absorb{.Class = "VisualEffect", .Function = "Play", .Self = LocalForm{0x280C0},
                     .Arguments = {LocalForm{0xFF000900}, 8.f, LocalForm{kGuest}}};
    MarkEachPlayer(absorb, kGuest);
    THEN("the absorber is marked, the dragon and the effect are not")
    {
        CHECK(std::get<LocalForm>(absorb.Arguments[2]).EachPlayer);
        CHECK(!std::get<LocalForm>(absorb.Arguments[0]).EachPlayer);
        CHECK(!std::get<LocalForm>(absorb.Self).EachPlayer);
    }
    GIVEN("no player's action behind it: the player is this game's")
    {
        LocalCall shake{.Class = "Game", .Function = "ShakeCamera", .Arguments = {LocalForm{kThisPlayer}, 0.5f, 2.f}};
        MarkEachPlayer(shake, 0);
        THEN("marked") CHECK(std::get<LocalForm>(shake.Arguments[0]).EachPlayer);
    }
}
