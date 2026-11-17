#include <Services/DevCommands.h>
#include <algorithm>
#include <catch2/catch.hpp>

using namespace DevCommands;

namespace
{
bool Has(const std::vector<std::string>& acLines, std::string_view aLine)
{
    return std::ranges::find(acLines, aLine) != acLines.end();
}
} // namespace

TEST_CASE("Dev commands: the chat's lines that name one, valid or not", "[dev-commands]")
{
    CHECK(IsDevCommand("/heal"));
    CHECK(IsDevCommand("/spawn"));
    CHECK(IsDevCommand("/spawn unicorn 3"));
    CHECK(IsDevCommand("/dev"));
    CHECK_FALSE(IsDevCommand("/tpa"));
    CHECK_FALSE(IsDevCommand("/bug the wolf froze"));
    CHECK_FALSE(IsDevCommand("heal"));
    CHECK_FALSE(IsDevCommand("/healing"));
}

TEST_CASE("Dev commands: a bad argument gives the command's usage, nothing to run", "[dev-commands]")
{
    for (const char* cpLine :
         {"/spawn", "/spawn unicorn", "/spawn wolf 0", "/spawn wolf 21", "/spawn wolf 2 3", "/speed", "/speed 11",
          "/speed fast", "/gravity 0", "/gravity 3.5", "/god maybe", "/killmove", "/killmove 101", "/killmove -1",
          "/weather sunny", "/lootbox 11", "/gear iron", "/heal now"})
    {
        const auto cParsed = Parse(cpLine);
        INFO(cpLine);
        CHECK_FALSE(cParsed.Run);
        CHECK(cParsed.Error.starts_with("Usage: /"));
    }
}

TEST_CASE("Dev commands: a world command is the host's; every player command runs on each player", "[dev-commands]")
{
    const auto cSpawn = Parse("/spawn  wolf 3").Run;
    REQUIRE(cSpawn);
    CHECK(IsWorld(cSpawn->What));
    CHECK(WorldLines(*cSpawn, "FF000801") == std::vector<std::string>{"FF000801.placeatme B83C2 3"});
    CHECK(WorldLines(*Parse("/spawn dragon").Run, "player") == std::vector<std::string>{"player.placeatme 5EACF 1"});
    CHECK(WorldLines(*Parse("/weather storm").Run, "player") == std::vector<std::string>{"fw C8220"});
    CHECK(IsWorld(Parse("/killall").Run->What));
    CHECK(PlayerLines(*cSpawn).empty());

    for (const char* cpLine :
         {"/heal", "/god", "/speed 2", "/gravity 0.5", "/shouts", "/gear", "/lootbox", "/killmove 50", "/reset"})
    {
        INFO(cpLine);
        const auto cCommand = Parse(cpLine).Run;
        REQUIRE(cCommand);
        CHECK_FALSE(IsWorld(cCommand->What));
        CHECK(WorldLines(*cCommand, "player").empty());
    }
}

TEST_CASE("Dev commands: each game's console lines on its own player", "[dev-commands]")
{
    const auto cHeal = PlayerLines(*Parse("/heal").Run);
    CHECK(cHeal == std::vector<std::string>{"player.restoreav health 100000", "player.restoreav magicka 100000",
                                            "player.restoreav stamina 100000"});

    const auto cSpeed = PlayerLines(*Parse("/speed 2.5").Run);
    CHECK(cSpeed.front() == "player.setav speedmult 250.000");
    CHECK(Has(cSpeed, "player.modav carryweight 1"));

    // Gravity scales falling (the INI's 1.35) and the jump to the height that gravity gives (fJumpHeightMin 76).
    const auto cGravity = PlayerLines(*Parse("/gravity 0.5").Run);
    CHECK(cGravity == std::vector<std::string>{"setini \"fInAirFallingCharGravityMult:Havok\" 0.675",
                                               "setgs fJumpHeightMin 152.000"});

    const auto cShouts = PlayerLines(*Parse("/shouts").Run);
    CHECK(cShouts.size() == 20 * 7 + 1);
    CHECK(Has(cShouts, "player.addshout 13E07"));
    CHECK(Has(cShouts, "player.teachword 13E22"));
    CHECK(Has(cShouts, "player.unlockword 5FB97"));
    CHECK(cShouts.back() == "player.setav shoutrecoverymult 0");

    const auto cGlass = PlayerLines(*Parse("/gear glass").Run);
    CHECK(Has(cGlass, "player.additem 139BE 50"));
    CHECK(Has(cGlass, "player.equipitem 139A5"));
    CHECK(PlayerLines(*Parse("/lootbox 3").Run) == std::vector<std::string>{"player.additem E88 3"});
    CHECK(PlayerLines(*Parse("/killmove 100").Run).empty());
}

TEST_CASE("Dev commands: god follows the engine's own; reset turns everything back whatever set it", "[dev-commands]")
{
    // The engine's god mode may be on already (the console's tgm, a harness, a game swapped in).
    CHECK(GodAfter(*Parse("/god").Run, false) == true);
    CHECK(GodAfter(*Parse("/god").Run, true) == false);
    CHECK(GodAfter(*Parse("/god on").Run, true) == true);
    CHECK(GodAfter(*Parse("/god off").Run, true) == false);
    CHECK(GodAfter(*Parse("/god off").Run, false) == false);
    CHECK(GodAfter(*Parse("/reset").Run, true) == false);
    CHECK_FALSE(GodAfter(*Parse("/heal").Run, true));
    CHECK(PlayerLines(*Parse("/god").Run).empty());

    const auto cReset = PlayerLines(*Parse("/reset").Run);
    CHECK(cReset == std::vector<std::string>{
                        "player.setav speedmult 100.000", "player.modav carryweight 1", "player.modav carryweight -1",
                        "setini \"fInAirFallingCharGravityMult:Havok\" 1.350", "setgs fJumpHeightMin 76.000"});
}

TEST_CASE("Dev commands: one kill move roll per attacker and target a second, whatever is asked between",
          "[dev-commands][kill-move]")
{
    KillMoveRolls rolls;
    uint32_t draws = 0;
    const auto cDraw = [&] { return 10 * ++draws; };
    CHECK(rolls.Roll(1, 2, 0, cDraw) == 10);   // A
    CHECK(rolls.Roll(3, 4, 100, cDraw) == 20); // B
    CHECK(rolls.Roll(1, 2, 200, cDraw) == 10); // A again: its own roll
    CHECK(rolls.Roll(3, 4, 900, cDraw) == 20);
    CHECK(draws == 2);
    // A second after its roll, A rolls again.
    CHECK(rolls.Roll(1, 2, 1000, cDraw) == 30);
    CHECK(draws == 3);
}

TEST_CASE("Dev commands: kill move odds answer the two conditions; none leaves the game's own",
          "[dev-commands][kill-move]")
{
    const auto cOdds = Parse("/killmove 30").Run;
    REQUIRE(cOdds);
    CHECK(cOdds->Odds == std::optional<uint8_t>(30));
    CHECK_FALSE(Parse("/killmove default").Run->Odds);

    // The game's own: a fatal hit on the last enemy.
    CHECK(ShouldAttackKill(true, std::nullopt, 99));
    CHECK_FALSE(ShouldAttackKill(false, std::nullopt, 0));
    CHECK_FALSE(IsLastHostileActor(false, std::nullopt));
    // Odds: a fatal hit whose roll is under them, on any enemy.
    CHECK(ShouldAttackKill(true, 30, 29));
    CHECK_FALSE(ShouldAttackKill(true, 30, 30));
    CHECK_FALSE(ShouldAttackKill(false, 100, 0));
    CHECK_FALSE(ShouldAttackKill(true, 0, 0));
    CHECK(ShouldAttackKill(true, 100, 99));
    CHECK(IsLastHostileActor(false, 30));
}

TEST_CASE("Dev commands: the console names this game's player and a copy; not an id it reads as a number",
          "[dev-commands]")
{
    CHECK(ConsoleName(0x14, 0x14) == "player");
    CHECK(ConsoleName(0xFF000801, 0x14) == "FF000801");
    CHECK_FALSE(ConsoleName(0x0001A695, 0x14));
    CHECK_FALSE(ConsoleName(0, 0x14));
}
