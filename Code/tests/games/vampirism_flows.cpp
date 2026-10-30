// Vampirism is each player's own (docs/design/system-quests.md): a guest's disease turns the guest only, through its
// own instance of PlayerVampireQuest; each player hungers on its own; the story's cure cures them all; a guest's
// vampirism is kept in its record.
#include "Party.h"
#include "Fixtures.h"

#include <Papyrus/SystemQuests.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>

namespace
{
constexpr uint32_t kSmoke = 0x30A4F;          // QASmoke
constexpr uint32_t kFalion = 0x5C625;         // VC01: Rising at Dawn
constexpr uint32_t kVampireQuest = 0xEAFD5;   // PlayerVampireQuest
constexpr uint32_t kPlayerIsVampire = 0xED06D; // its script's global
constexpr uint32_t kAbVampire = 0x2E1C3, kAbVampire02 = 0x2E1C4; // AbVampire01, 02
constexpr uint32_t kNord = 0x13746, kKhajiit = 0x13745;
constexpr uint32_t kNordVampire = 0x88794, kKhajiitVampire = 0x88845;

int32_t Status(FakeEngine& aGame)
{
    const auto& cVariables = aGame.QuestVariables[kVampireQuest];
    const auto it = cVariables.find("VampireStatus");
    return it != cVariables.end() ? std::get<int32_t>(it->second) : 0;
}

// PlayerVampireQuestScript, each game's instance its own player's: VampireChange (the target's race's vampire, its
// cure race kept, the hunger's clock started), its timer (a stage further), VampireFeed (back to the first) and
// VampireCure (the cure race, if it is one).
void VampireQuest(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.QuestGlobals[kVampireQuest] = {kPlayerIsVampire};
    aEngine.Globals[kPlayerIsVampire] = 0.f;
    const auto cOn = [](const LocalCall& acCall) { return std::get<LocalForm>(acCall.Arguments.at(0)).FormId; };
    const auto cSetValue = [](FakeEngine& aGame, float aValue, uint32_t aCause) {
        aGame.RunScript({.Class = "GlobalVariable", .Function = "SetValue", .Self = LocalForm{kPlayerIsVampire}, .Arguments = {aValue}}, aCause);
    };
    aEngine.QuestFunctions[{kVampireQuest, "VampireChange"}] = [=](FakeEngine& aGame, const LocalCall& acCall, uint32_t aCause) {
        const uint32_t cTarget = cOn(acCall);
        const uint32_t cRace = aGame.GetActorRaceFormId(cTarget);
        auto& variables = aGame.QuestVariables[kVampireQuest];
        variables["CureRace"] = LocalForm{cRace};
        variables["VampireStatus"] = int32_t{1};
        aGame.RunScript({.Class = "Actor", .Function = "SetRace", .Self = LocalForm{cTarget},
                         .Arguments = {LocalForm{cRace == kKhajiit ? kKhajiitVampire : kNordVampire}}},
                        aCause);
        aGame.AddsSpell(cTarget, kAbVampire, true, aCause);
        cSetValue(aGame, 1.f, aCause);
        SystemQuests::TimerCalled(kVampireQuest, "RegisterForUpdateGameTime", 12.f);
        // The cure quest again for one cured before (Quest.SetStage: a script function over SetCurrentStageID).
        aGame.CallsQuest({.Class = "Quest", .Function = "SetStage", .Self = LocalForm{kFalion}, .Arguments = {int32_t{25}}}, aCause);
    };
    aEngine.QuestFunctions[{kFalion, "SetStage"}] = [](FakeEngine&, const LocalCall&, uint32_t) {};
    aEngine.QuestFunctions[{kVampireQuest, "OnUpdateGameTime"}] = [](FakeEngine& aGame, const LocalCall&, uint32_t aCause) {
        if (Status(aGame) != 1)
            return;
        aGame.QuestVariables[kVampireQuest]["VampireStatus"] = int32_t{2};
        aGame.AddsSpell(FakeEngine::kPlayer, kAbVampire02, true, aCause);
    };
    aEngine.QuestFunctions[{kVampireQuest, "VampireFeed"}] = [](FakeEngine& aGame, const LocalCall&, uint32_t aCause) {
        aGame.QuestVariables[kVampireQuest]["VampireStatus"] = int32_t{1};
        aGame.AddsSpell(FakeEngine::kPlayer, kAbVampire02, false, aCause);
    };
    aEngine.QuestFunctions[{kVampireQuest, "VampireCure"}] = [=](FakeEngine& aGame, const LocalCall& acCall, uint32_t aCause) {
        const uint32_t cPlayer = cOn(acCall);
        auto& variables = aGame.QuestVariables[kVampireQuest];
        // Only a vampire's race changes (the script's own check).
        const uint32_t cRace = aGame.GetActorRaceFormId(cPlayer);
        if ((cRace == kNordVampire || cRace == kKhajiitVampire) && variables.contains("CureRace"))
            aGame.RunScript({.Class = "Actor", .Function = "SetRace", .Self = LocalForm{cPlayer}, .Arguments = {variables["CureRace"]}}, aCause);
        variables["VampireStatus"] = int32_t{0};
        aGame.AddsSpell(cPlayer, kAbVampire, false, aCause);
        aGame.AddsSpell(cPlayer, kAbVampire02, false, aCause);
        cSetValue(aGame, 0.f, aCause);
        SystemQuests::TimerCalled(kVampireQuest, "UnregisterForUpdateGameTime", 0.f);
    };
}

LocalCall VampireCall(const char* acpFunction, std::vector<LocalValue> aArguments = {LocalForm{FakeEngine::kPlayer}})
{
    return {.Class = "PlayerVampireQuestScript", .Function = acpFunction, .Self = LocalForm{kVampireQuest}, .Arguments = std::move(aArguments)};
}

uint32_t RaceOf(const FakeEngine& acEngine)
{
    return acEngine.GetActorRaceFormId(FakeEngine::kPlayer);
}

bool Has(FakeEngine& aGame, uint32_t aSpell)
{
    return aGame.AddedSpells[FakeEngine::kPlayer].contains(aSpell);
}

// The guest's disease completes: its effect's script (a body's) calls PlayerVampireQuest.VampireChange.
void GuestTurns(Party& aParty, World& aHost, World& aGuest)
{
    auto& hostEngine = Party::Engine(aHost);
    auto& guestEngine = Party::Engine(aGuest);
    aParty.As(aGuest, [&] { guestEngine.CallsQuest(VampireCall("VampireChange"), FakeEngine::kPlayer, false, true); });
    aParty.Until([&] { return RaceOf(guestEngine) == kKhajiitVampire && hostEngine.SystemQuestList.Contains(kVampireQuest); },
                 "the guest's player is a vampire, and the host's game learned the quest is one per player");
}
} // namespace

SCENARIO("A guest's vampirism is its own", "[vampirism][system-quests]")
{
    Party party(1, VampireQuest);
    World& host = party.Host();
    World& guest = party.Guest();
    auto& hostEngine = Party::Engine(host);
    auto& guestEngine = Party::Engine(guest);
    hostEngine.PlayerSwitchesRace(kNord);
    party.As(guest, [&] { guestEngine.PlayerSwitchesRace(kKhajiit); });
    party.Until([&] { return PlayerCopy(hostEngine) && hostEngine.GetActorRaceFormId(PlayerCopy(hostEngine)) == kKhajiit; },
                "the host's copy of the guest's player is a Khajiit");

    WHEN("the guest's disease turns it")
    {
        GuestTurns(party, host, guest);
        party.Until([&] { return hostEngine.GetActorRaceFormId(PlayerCopy(hostEngine)) == kKhajiitVampire; },
                    "the host's copy of the guest's player is a Khajiit vampire");
        THEN("Falion's quest, whose stage the vampire quest set, stays the story's")
            CHECK((!guestEngine.SystemQuestList.Contains(kFalion) && !hostEngine.SystemQuestList.Contains(kFalion)));
        THEN("only the guest is a vampire: the host's player stays a human Nord")
        {
            CHECK(Has(guestEngine, kAbVampire));
            CHECK(RaceOf(hostEngine) == kNord);
            CHECK_FALSE(Has(hostEngine, kAbVampire));
            CHECK(Status(hostEngine) == 0);
        }
        THEN("its global is the guest's own, and stays so through the host's world state")
        {
            party.As(host, [&] { hostEngine.SetGlobal(kPlayerIsVampire, 0.f); });
            for (int i = 0; i < 10; ++i)
                party.Frame();
            CHECK(guestEngine.Globals[kPlayerIsVampire] == 1.f);
            CHECK(hostEngine.Globals[kPlayerIsVampire] == 0.f);
        }

        AND_WHEN("each game's timer fires (its own instance's)")
        {
            party.As(host, [&] { hostEngine.FiresQuestTimer(kVampireQuest, "OnUpdateGameTime"); });
            party.As(guest, [&] { guestEngine.FiresQuestTimer(kVampireQuest, "OnUpdateGameTime"); });
            party.Until([&] { return Has(guestEngine, kAbVampire02); }, "the guest's hunger reaches its second stage");
            THEN("the host's player does not hunger") CHECK_FALSE(Has(hostEngine, kAbVampire02));

            AND_WHEN("the guest feeds on the host's NPC: the host runs the feed's fragment for it")
            {
                party.As(host, [&] { hostEngine.CallsQuest(VampireCall("VampireFeed", {}), PlayerCopy(hostEngine)); });
                party.Until([&] { return !Has(guestEngine, kAbVampire02); }, "the guest's hunger is back at its first stage");
                THEN("the host's instance did not run it") CHECK(Status(hostEngine) == 0);
            }
        }

        AND_WHEN("the story cures (Falion's stage: no player's action)")
        {
            party.As(host, [&] { hostEngine.CallsQuest(VampireCall("VampireCure")); });
            party.Until([&] { return RaceOf(guestEngine) == kKhajiit && !Has(guestEngine, kAbVampire); }, "the guest's player is cured");
            THEN("every player is its own race again, and no one is a vampire")
            {
                CHECK(RaceOf(hostEngine) == kNord);
                CHECK(guestEngine.Globals[kPlayerIsVampire] == 0.f);
                CHECK(Status(guestEngine) == 0);
            }
        }
    }
}

SCENARIO("A guest's vampirism is kept across leaving and joining again", "[vampirism][system-quests][join]")
{
    Party party(1, VampireQuest, false);
    World& host = party.Host();
    World& guest = party.Guest();
    auto& hostEngine = Party::Engine(host);
    auto& guestEngine = Party::Engine(guest);
    const auto cJoins = [&](const char* acpStep) {
        party.As(guest, [&] {
            REQUIRE(guest.ctx().at<JoinService>().Join(std::format("127.0.0.1:{}", host.GetHostService().GetNetwork()->GetPort())));
        });
        party.Until([&] { return guest.GetTransport().IsOnline() && !guest.ctx().at<JoinService>().IsJoining(); }, acpStep);
    };
    hostEngine.PlayerSwitchesRace(kNord);
    guestEngine.PlayerSwitchesRace(kKhajiit);
    cJoins("the friend's game joins with the host's world");
    GuestTurns(party, host, guest);
    party.Until(
        [&] {
            const PlayerRecord* cpKept = host.ctx().at<SaveService>().GetRecords().Find(guestEngine.PlayerId());
            const auto cQuests = cpKept ? cpKept->Extras.find(SystemQuestState::kExtra) : std::map<std::string, std::string>::const_iterator{};
            const auto cState = cpKept && cQuests != cpKept->Extras.end() ? SystemQuestState::Decode(cQuests->second) : std::nullopt;
            const auto* cpQuest = cState ? cState->Find(GameId(0, kVampireQuest)) : nullptr;
            return cpQuest && std::ranges::any_of(cpQuest->Variables, [](const auto& acVariable) {
                       return acVariable.Name == "VampireStatus" && acVariable.Value == ScriptValue{int32_t{1}};
                   });
        },
        "the host's game keeps the friend's record with its vampirism");

    WHEN("the host turns too, and the friend leaves and joins again from the main menu")
    {
        party.As(host, [&] { hostEngine.CallsQuest(VampireCall("VampireChange"), FakeEngine::kPlayer, false, true); });
        party.Until([&] { return RaceOf(hostEngine) == kNordVampire; }, "the host's player is a vampire");
        party.As(host, [&] { hostEngine.FiresQuestTimer(kVampireQuest, "OnUpdateGameTime"); });
        party.Until([&] { return Status(hostEngine) == 2; }, "the host's hunger is at its second stage");
        party.Leave(guest);
        party.Until([&] { return !guest.GetTransport().IsConnected(); }, "the friend's game left");
        guestEngine.AtMainMenu = true;
        cJoins("the friend's game joins again");
        THEN("its instance is its own again, not the host's save's")
        {
            CHECK(Status(guestEngine) == 1);
            CHECK(guestEngine.Globals[kPlayerIsVampire] == 1.f);
            CHECK(guestEngine.QuestTimers[kVampireQuest].contains("RegisterForUpdateGameTime"));
            CHECK(guestEngine.SystemQuestList.Contains(kVampireQuest));
        }
    }
}

SCENARIO("A friend who was never a vampire joins a host who is: its instance starts over", "[vampirism][system-quests][join]")
{
    Party party(1, VampireQuest, false);
    World& host = party.Host();
    World& guest = party.Guest();
    auto& hostEngine = Party::Engine(host);
    auto& guestEngine = Party::Engine(guest);
    hostEngine.PlayerSwitchesRace(kNord);
    party.As(host, [&] { hostEngine.CallsQuest(VampireCall("VampireChange"), FakeEngine::kPlayer, false, true); });
    party.Until([&] { return Status(hostEngine) == 1; }, "the host's player is a vampire");
    party.As(guest, [&] {
        REQUIRE(guest.ctx().at<JoinService>().Join(std::format("127.0.0.1:{}", host.GetHostService().GetNetwork()->GetPort())));
    });
    party.Until([&] { return guest.GetTransport().IsOnline() && !guest.ctx().at<JoinService>().IsJoining(); },
                "the friend's game joins with the host's world");
    THEN("the quest is one per player there too, and the friend's instance is not the host's save's")
    {
        CHECK(guestEngine.SystemQuestList.Contains(kVampireQuest));
        CHECK(Status(guestEngine) == 0);
        CHECK(guestEngine.QuestTimers[kVampireQuest].empty());
        CHECK(Status(hostEngine) == 1);
    }
}
