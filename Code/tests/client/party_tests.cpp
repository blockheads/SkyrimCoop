// PartyRules: whose a controls, screen or progression call is, and who holds the controls (docs/design/cutscenes.md,
// docs/design/dragonborn.md).
#include <catch2/catch.hpp>

#include "Routes.h"

using namespace Routes;

TEST_CASE("Barter on another actor opens on the picker's game", "[party][route][barter]")
{
    CallFacts barter{.Reach = {Call::kPlayerMenu}, .Cause = 0xFF000801, .GuestCause = true, .Hosting = true};
    CHECK(RouteOf(barter) == Routing{.To = Destination::kCauseGame, .Original = false});
    barter.Story = true;
    CHECK(RouteOf(barter) == Routing{.To = Destination::kCauseGame, .Original = false});
    barter.GuestCause = false;
    barter.Cause = 0x14;
    CHECK(RouteOf(barter) == Routing{});
    barter.Cause = 0;
    CHECK(RouteOf(barter) == Routing{});
    barter.Cause = MirroredTag(1);
    CHECK(RouteOf(barter) == Routing{});
}

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
        CHECK(ShareOf(Call::kShow, kGuest, false, 0) == Share::kParty);
        CHECK(ShareOf(Call::kShow, kThisPlayer, false, 0) == Share::kParty);
        CHECK(ShareOf(Call::kShow, 0, false, 0) == Share::kParty);
    }
    THEN("another game's, mirrored here, runs here only") CHECK(ShareOf(Call::kShow, kHostsGame, false, 0) == Share::kHere);
}

SCENARIO("A controls call is shared only if it takes movement", "[party]")
{
    WHEN("a world script takes the movement")
    THEN("the party's") CHECK(ShareOf(Call::kControls, 0, false, kCutscene) == Share::kParty);

    WHEN("it takes fighting only (a restriction)")
    THEN("this game's alone") CHECK(ShareOf(Call::kControls, 0, false, kFighting) == Share::kHere);

    WHEN("it takes sneaking and menus")
    THEN("this game's alone") CHECK(ShareOf(Call::kControls, 0, false, kSneaking | kMainFour) == Share::kHere);

    WHEN("a screen call (a fade) asks for no controls")
    THEN("still the party's") CHECK(ShareOf(Call::kScreen, 0, false, 0) == Share::kParty);

    WHEN("a player's own moment takes the movement")
    THEN("still theirs") CHECK(ShareOf(Call::kControls, kThisPlayer, false, kMovement) == Share::kPlayer);

    WHEN("a restriction is a player's own")
    THEN("theirs, not a restriction of this game")
    CHECK(ShareOf(Call::kControls, kGuest, false, kFighting) == Share::kPlayer);
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
    GIVEN("a word of power taught or unlocked by a script (a reward)")
    {
        THEN("it is the party's, whoever's action ran it (a guest's word wall, Paarthurnax)")
        {
            CHECK(Relays(ProgressRoute(kReward, Target::kThisPlayer, 0)));
            CHECK(Relays(ProgressRoute(kReward, Target::kThisPlayer, kGuest)));
            CHECK(Relays(ProgressRoute(kReward, Target::kThisPlayer, kThisPlayer)));
        }
        THEN("the host's own player takes it there as the original: nothing more runs here")
        {
            const Routing cRun = ProgressRoute(kReward, Target::kThisPlayer, kGuest);
            CHECK(cRun.Original);
            CHECK(!cRun.ThisPlayer);
            CHECK(cRun.MarkFor == kThisPlayer);
        }
        THEN("another game's, run here, is not sent again; a guest's game's own scripts are not the story")
        {
            CHECK(!Relays(ProgressRoute(kReward, Target::kThisPlayer, kHostsGame)));
            CHECK(!Relays(ProgressRoute(kReward, Target::kThisPlayer, kGuestsGame)));
            CHECK(!Relays(ProgressRoute(kReward, Target::kThisPlayer, 0, false, false, false)));
        }
    }
    GIVEN("a dragon's soul absorbed into a guest's character (MQKillDragonScript: AbsorbActor.ModActorValue)")
    {
        const Routing cRun = ProgressRoute(kReward, Target::kGuestCharacter, kGuest, true);
        THEN("every game's own player gains it; the character itself not: its owner's game supplies its values")
        {
            CHECK(Relays(cRun));
            CHECK(cRun.ThisPlayer);
            CHECK(!cRun.Original);
        }
    }
    GIVEN("a faction or a spell on a player")
    {
        THEN("the story's is the party's: no player behind it, or a stage a player's action set")
        {
            CHECK(Relays(ProgressRoute(kStoryOnCopy, Target::kThisPlayer, 0)));
            CHECK(Relays(ProgressRoute(kStoryOnCopy, Target::kGuestCharacter, kGuest, true)));
        }
        THEN("on a guest's character the original runs too: the host's conditions read it there")
        {
            const Routing cRun = ProgressRoute(kStoryOnCopy, Target::kGuestCharacter, kGuest, true);
            CHECK(cRun.Original);
            CHECK(cRun.ThisPlayer);
        }
        THEN("a player's own moment stays theirs: its own action (a lever), or a body's script (its beast form's quest)")
        {
            CHECK(!Relays(ProgressRoute(kStoryOnCopy, Target::kGuestCharacter, kGuest)));
            CHECK(!Relays(ProgressRoute(kStoryOnCopy, Target::kThisPlayer, kThisPlayer, true, true)));
            CHECK(!Relays(ProgressRoute(kStoryOnCopy, Target::kThisPlayer, 0, false, true)));
        }
        THEN("on an NPC it is no player's") CHECK(!Relays(ProgressRoute(kStoryOnCopy, Target::kNone, 0)));
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

// docs/design/party-calls.md: one decision for every native's call.
SCENARIO("Where a native's call runs", "[party][route]")
{
    GIVEN("a guest's own script changing the world (a quest's stage)")
    {
        const CallFacts cFacts{.Reach = {Call::kWorld}, .Cause = kThisPlayer, .Guest = true};
        THEN("the host's game makes it, not this one") CHECK(RouteOf(cFacts) == Routing{.To = Destination::kHost, .Original = false});
        THEN("a quest of its own keeps its stages here")
        {
            CallFacts own = cFacts;
            own.OwnQuest = true;
            CHECK(RouteOf(own) == Routing{});
        }
        THEN("on the host it is made here") CHECK(RouteOf({.Reach = {Call::kWorld}, .Hosting = true}) == Routing{});
    }
    GIVEN("a native on the player in a script a guest's action ran on the host")
    {
        const CallFacts cFacts{.PlayerNative = true, .Cause = kGuest, .GuestCause = true, .Hosting = true};
        THEN("that guest's game runs it, on its own player")
        CHECK(RouteOf(cFacts) == Routing{.To = Destination::kCauseGame, .Original = false});
        THEN("a native not on the player runs here")
        {
            CallFacts other = cFacts;
            other.PlayerNative = false;
            CHECK(RouteOf(other) == Routing{});
        }
        THEN("a screen call of its own moment goes to it too, a method's too (its lever's Message.Show, answered there)")
        {
            CallFacts box = cFacts;
            box.Reach = {Call::kScreen};
            box.PlayerNative = false;
            CHECK(RouteOf(box) == Routing{.To = Destination::kCauseGame, .Original = false});
        }
        THEN("an item change in its stack runs here: the copy carries its items (party-model.md, Items)")
        {
            CallFacts equip = cFacts;
            equip.Reach = {Call::kItem};
            equip.PlayerNative = false;
            CHECK(RouteOf(equip) == Routing{});
        }
    }
    GIVEN("the screen, the controls or a show")
    {
        THEN("the story's runs on every game, its player each game's own")
        {
            CHECK(RouteOf({.Reach = {Call::kScreen}, .Hosting = true}) ==
                  Routing{.To = Destination::kEveryGame, .Mark = true, .MarkFor = 0});
            CHECK(RouteOf({.Reach = {Call::kControls}, .Cause = kGuest, .Story = true}).To == Destination::kEveryGame);
        }
        THEN("a player's own moment stays here") CHECK(RouteOf({.Reach = {Call::kScreen}, .Cause = kThisPlayer}) == Routing{});
        THEN("a show is every game's whoever's action ran it, its player not marked")
        CHECK(RouteOf({.Reach = {Call::kShow}, .Cause = kThisPlayer}) == Routing{.To = Destination::kEveryGame, .MarkFor = kThisPlayer});
    }
    THEN("another game's call, run here, runs here only")
    {
        CHECK(RouteOf({.Reach = {Call::kShow}, .Cause = kHostsGame}) == Routing{});
        CHECK(RouteOf({.Reach = {Call::kWorld}, .Cause = kHostsGame, .Guest = true}) == Routing{});
        CHECK(RouteOf({.PlayerNative = true, .Cause = kGuestsGame, .GuestCause = true}) == Routing{});
    }
    THEN("a native nothing reaches runs here") CHECK(RouteOf({.Cause = kGuest, .Hosting = true}) == Routing{});
}

// docs/design/party-calls.md, "A box's answer": a story's message box on the guest closes with the host's answer.
SCENARIO("Another game's latent call here ends with its original's result", "[party][messages]")
{
    LatentCopies copies;
    constexpr uint32_t cHost = 0;
    constexpr uint32_t cOther = 2;
    const LocalValue cYes = int32_t{1};
    GIVEN("two of the host's boxes, sent and started in order")
    {
        copies.Sent(cHost, 101);
        copies.Sent(cHost, 102);
        CHECK(!copies.Started(cHost, 7));
        CHECK(!copies.Started(cHost, 8));
        THEN("each original's end finds its own copy, once, in any order")
        {
            CHECK(copies.Ended(cHost, 102, cYes) == 8u);
            CHECK(copies.Ended(cHost, 101, cYes) == 7u);
            CHECK(!copies.Ended(cHost, 101, cYes));
        }
        THEN("a copy its player answered here first is forgotten")
        {
            copies.Returned(7);
            CHECK(!copies.Ended(cHost, 101, cYes));
            CHECK(copies.Ended(cHost, 102, cYes) == 8u);
        }
        THEN("another game's keys are its own")
        {
            CHECK(!copies.Ended(cOther, 101, cYes));
            copies.Left(cHost);
            CHECK(!copies.Ended(cHost, 102, cYes));
        }
    }
    GIVEN("an original that ended before its copy started here")
    {
        copies.Sent(cHost, 101);
        copies.Sent(cHost, 102);
        CHECK(!copies.Ended(cHost, 101, cYes));
        THEN("its copy ends with the result as it starts; the next copy is the next call's")
        {
            CHECK(copies.Started(cHost, 9) == cYes);
            CHECK(!copies.Started(cHost, 10));
            CHECK(copies.Ended(cHost, 102, cYes) == 10u);
        }
    }
}

// party-model.md rule 3; party-calls.md, "Questions".
SCENARIO("A question about the player with no player's action is asked of the party", "[party][route]")
{
    const CallFacts cTimer{.Reach = {Call::kQuestion}, .Hosting = true, .PlayerInCall = true};
    THEN("a quest's timer asks the party") CHECK(RouteOf(cTimer) == Routing{.Ask = true});
    THEN("a player's own action asks about that player (rule 2), the story too")
    {
        CallFacts own = cTimer;
        own.Cause = kGuest;
        CHECK(RouteOf(own) == Routing{});
        own.Story = true;
        CHECK(RouteOf(own) == Routing{});
    }
    THEN("not about the player, or not the host's script, or another game's: as it is")
    {
        CallFacts npc = cTimer;
        npc.PlayerInCall = false;
        CHECK(RouteOf(npc) == Routing{});
        CallFacts guest = cTimer;
        guest.Hosting = false;
        CHECK(RouteOf(guest) == Routing{});
        CallFacts mirrored = cTimer;
        mirrored.Cause = kHostsGame;
        CHECK(RouteOf(mirrored) == Routing{});
    }
}

SCENARIO("Which player a question asks about", "[party][route]")
{
    THEN("the nearest to the reference it measures from")
    CHECK(AskedPlayer({{kThisPlayer, 900.f}, {kGuest, 120.f}, {0xFF000802, std::nullopt}}) == kGuest);
    THEN("with no reference, one in the location the script names")
    CHECK(AskedPlayer({{kThisPlayer, std::nullopt, false}, {kGuest, std::nullopt, true}}) == kGuest);
    THEN("a reference counts over a place") CHECK(AskedPlayer({{kThisPlayer, 50.f, false}, {kGuest, std::nullopt, true}}) == kThisPlayer);
    THEN("nothing named: this game's player") CHECK(!AskedPlayer({{kThisPlayer}, {kGuest}}));
}

SCENARIO("Whom a question asked of the party is put to", "[party][route]")
{
    const std::vector<uint32_t> cGuests{kGuest, 0xFF000802};
    THEN("a yes/no question: every player, this game's first, until one says yes")
    CHECK(PlayersToAsk(true, std::nullopt, cGuests) == std::vector<uint32_t>{kThisPlayer, kGuest, 0xFF000802});
    THEN("any other: the one picked, else this game's player")
    {
        CHECK(PlayersToAsk(false, kGuest, cGuests) == std::vector<uint32_t>{kGuest});
        CHECK(PlayersToAsk(false, std::nullopt, cGuests) == std::vector<uint32_t>{kThisPlayer});
    }
}
