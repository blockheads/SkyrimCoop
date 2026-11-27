// Host loads reuse the wipe's transfer and rejoin; guest-initiated loads are refused in chat.
#include "Party.h"

#include <join/GameLoadFailedEvent.h>
#include <core/MenuEvent.h>
#include <core/SessionNoticeEvent.h>
#include <join/JoinService.h>
#include <join/SaveNames.h>
#include <join/SaveService.h>
#include <join/SaveTransfer.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.Player.Level = 7;
    aEngine.References[0xECA].Cell = kSmoke;
    aEngine.References[0xECA].Locked = true;
}

void Save(Party& aParty, const std::string& acName)
{
    auto& host = Party::Engine(aParty.Host());
    aParty.As(aParty.Host(), [&] { (void)host.SaveNow(acName); });
    aParty.Until([&] { return host.MadeSave.has_value(); }, "the host writes the save and co-save");
    aParty.As(aParty.Host(), [&] { REQUIRE(host.SaveNow(acName).has_value()); });
}

// Every game has loaded aRound saves and is back in the session.
void Reloads(Party& aParty, size_t aRound, const std::string& acStep)
{
    aParty.Until([&] {
        for (size_t i = 0; i < aParty.GameCount(); ++i)
        {
            auto& game = i ? aParty.Guest(i - 1) : aParty.Host();
            if (Party::Engine(game).Loaded.size() != aRound || !game.GetTransport().IsOnline() || game.ctx().at<JoinService>().IsJoining())
                return false;
        }
        return true;
    }, acStep, {}, nullptr, std::chrono::seconds{static_cast<int64_t>(JoinService::kLoadSeconds)});
}

// The id of the image of one of the host's saves, and whether a guest's store holds it.
std::string ImageOf(Party& aParty, const std::string& acName)
{
    return SaveTransfer::MakeImage(Party::Engine(aParty.Host()).SavedFiles.at(acName))->Id;
}

bool Holds(World& aGuest, const std::string& acImage)
{
    const auto cHeld = Party::Engine(aGuest).HeldSaves();
    return std::ranges::find(cHeld, acImage) != cHeld.end();
}

// As a quicksave: made now, its file written a frame later; then every guest holds it.
void Quicksave(Party& aParty, const std::string& acName)
{
    auto& host = Party::Engine(aParty.Host());
    aParty.As(aParty.Host(), [&] { REQUIRE(host.Quicksaves(acName)); });
    CHECK_FALSE(host.SavedFiles.contains(acName));
    aParty.Until([&] {
        if (!host.SavedFiles.contains(acName))
            return false;
        for (size_t i = 1; i < aParty.GameCount(); ++i)
            if (!Holds(aParty.Guest(i - 1), ImageOf(aParty, acName)))
                return false;
        return true;
    }, "the quicksave is written and every guest holds it");
}

std::string Noise(size_t aSize, uint32_t aSeed)
{
    std::string data(aSize, '\0');
    for (auto& c : data)
    {
        aSeed ^= aSeed << 13;
        aSeed ^= aSeed >> 17;
        aSeed ^= aSeed << 5;
        c = static_cast<char>(aSeed);
    }
    return data;
}

struct Notices
{
    explicit Notices(World& aGame)
        : Connection(aGame.GetDispatcher().sink<SessionNoticeEvent>().connect<&Notices::On>(*this))
    {
    }
    void On(const SessionNoticeEvent& acNotice) { Lines.push_back(acNotice.Text); }
    std::vector<std::string> Lines;
    entt::scoped_connection Connection;
};
} // namespace

SCENARIO("A host load restores the chosen save and every guest at its saved place", "[load][join]")
{
    Party party(3, Plugins);
    auto& host = Party::Engine(party.Host());
    for (size_t i = 0; i < 3; ++i)
        Party::Engine(party.Guest(i)).Actors.at(FakeEngine::kPlayer).Position[0] = 500.f * (i + 1);
    party.Agree("every guest's saved place reaches the host");
    const std::string cChosen = GENERATE("Quicksave0", "MenuSave");
    Save(party, cChosen);

    party.As(party.Host(), [&] { host.SetLock(0xECA, false, 0); });
    for (size_t i = 0; i < 3; ++i)
        party.As(party.Guest(i), [&] {
            Party::Engine(party.Guest(i)).Player.Level = 22;
            party.Guest(i).GetDispatcher().trigger(MenuEvent{"InventoryMenu", false});
        });
    party.Until([&] { return !Party::Engine(party.Guest()).References.at(0xECA).Locked; }, "the world changes after the chosen save");
    Save(party, "NewerSave");
    const size_t cSaves = host.Saves.size();
    auto* cpListener = party.Host().GetHostService().GetNetwork();

    // Both loads use the same route, including a second load after the new authority has taken over.
    for (size_t round = 1; round <= 2; ++round)
    {
        party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads(cChosen)); });
        CHECK(host.Loaded.size() == round - 1);
        party.Until([&] {
            for (size_t i = 0; i < party.GameCount(); ++i)
            {
                auto& game = i ? party.Guest(i - 1) : party.Host();
                if (Party::Engine(game).Loaded.size() != round || !game.GetTransport().IsOnline() || game.ctx().at<JoinService>().IsJoining())
                    return false;
            }
            return true;
        }, "the whole party loads the chosen save and rejoins", {}, nullptr,
            std::chrono::seconds{static_cast<int64_t>(JoinService::kLoadSeconds)});
        CHECK(party.Host().GetHostService().GetNetwork() == cpListener);
        CHECK(host.Saves.size() == cSaves);
        CHECK(host.References.at(0xECA).Locked);
        const char* cNames[] = {"Lydia", "Aela", "Serana"};
        for (size_t i = 0; i < 3; ++i)
        {
            const auto& guest = Party::Engine(party.Guest(i));
            CHECK(guest.Player.Name == cNames[i]);
            CHECK(guest.Player.Level == 7);
            CHECK(guest.References.at(0xECA).Locked);
            CHECK(guest.Actors.at(FakeEngine::kPlayer).Position[0] == Approx(500.f * (i + 1)));
        }
        party.Agree("every restored character has its copies");
    }
}

SCENARIO("A host load makes the chosen save's character the engine's, as loading it alone would", "[load]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    const std::string cChosen = "Save12_0BADF00D_0_4C79646961_WhiterunWorld_000030_20261108213000_1_1";
    Save(party, cChosen);
    host.CharacterId = 0xABC467A6;
    party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads(cChosen)); });
    party.Until([&] { return host.Loaded.size() == 1 && party.Host().GetTransport().IsOnline(); },
                "the host loads the chosen save under the scratch name");
    CHECK(host.Loaded.back() == "SkyrimCoopWipe");
    CHECK(host.CharacterId == 0x0BADF00D);
}

SCENARIO("A guest's own loads are refused with a chat reason, including internal-looking names", "[load]")
{
    Party party(1, Plugins);
    auto& game = party.Guest();
    auto& guest = Party::Engine(game);
    Notices notices(game);
    for (const auto* cpName : {"Quicksave0", "MenuSave", "SkyrimCoopJoin", "SkyrimCoopWipe"})
        party.As(game, [&] { CHECK_FALSE(guest.PlayerLoads(cpName)); });
    CHECK(guest.Loaded.empty());
    CHECK_FALSE(guest.PendingLoad.has_value());
    CHECK(game.GetTransport().IsOnline());
    CHECK(notices.Lines == std::vector<std::string>(4, SaveService::kLoadRefusal));

    WHEN("the guest leaves, its own loads work again")
    {
        Save(party, "OfflineSave");
        guest.SavedFiles["OfflineSave"] = Party::Engine(party.Host()).SavedFiles.at("OfflineSave");
        party.Leave(game);
        party.As(game, [&] { CHECK(guest.PlayerLoads("OfflineSave")); });
        party.Until([&] { return guest.Loaded.size() == 1; }, "the offline game loads its own save");
    }
}

SCENARIO("A game joining a session refuses its own loads", "[load][join]")
{
    Party party(1, Plugins, false);
    auto& guest = party.Guest();
    Notices notices(guest);
    party.As(guest, [&] {
        REQUIRE(guest.ctx().at<JoinService>().Join(std::format("127.0.0.1:{}", party.Host().GetHostService().GetNetwork()->GetPort())));
        CHECK_FALSE(Party::Engine(guest).PlayerLoads("Quicksave0"));
    });
    CHECK(notices.Lines == std::vector<std::string>{SaveService::kLoadRefusal});
    party.Until([&] { return guest.GetTransport().IsOnline() && !guest.ctx().at<JoinService>().IsJoining(); },
                "the authorized join's own load still succeeds");
    CHECK(Party::Engine(guest).Loaded.size() == 1);
}

SCENARIO("An unreadable host save leaves the party playing and a later valid load succeeds", "[load]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    Notices notices(party.Host());
    party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads("MissingSave")); });
    party.Until([&] { return !notices.Lines.empty(); }, "the host sees why the save could not be read");
    CHECK(notices.Lines.back() == "The selected save could not be read. Choose another save.");
    CHECK(host.Loaded.empty());
    CHECK(Party::Engine(party.Guest()).Loaded.empty());
    CHECK(party.Host().GetTransport().IsOnline());
    CHECK(party.Guest().GetTransport().IsOnline());
    Save(party, "ValidSave");
    party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads("ValidSave")); });
    party.Until([&] { return host.Loaded.size() == 1 && Party::Engine(party.Guest()).Loaded.size() == 1 &&
                            party.Host().GetTransport().IsOnline() && party.Guest().GetTransport().IsOnline(); },
                "the valid save loads and the party rejoins");
}

SCENARIO("A guest reconnecting after a failed host load is refused instead of joining the old world", "[load][join]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    Notices notices(party.Host());
    Save(party, "BeforeLoad");
    party.As(party.Host(), [&] { (void)host.PlayerLoads("BeforeLoad"); });
    party.Until([&] { return host.PendingLoad.has_value() && guest.Loaded.size() == 1; },
                "the guest loads while the host's engine is about to load", {}, nullptr,
                std::chrono::seconds{static_cast<int64_t>(JoinService::kLoadSeconds)});
    party.As(party.Host(), [&] {
        host.PendingLoad.reset();
        party.Host().GetDispatcher().trigger(GameLoadFailedEvent{"SkyrimCoopWipe"});
    });
    auto& join = party.Guest().ctx().at<JoinService>();
    party.Until([&] { return !join.IsJoining(); }, "the guest's pending reconnect is refused");
    CHECK(join.GetStatus().Reason == "transfer_failed");
    CHECK(host.Loaded.empty());
    REQUIRE_FALSE(notices.Lines.empty());
    CHECK(notices.Lines.back() == "The save could not be loaded. Choose a save or return to the menu.");

    // A later valid choice can complete; the refused guest must join with that save, not its failed one.
    party.As(party.Host(), [&] { host.SetLock(0xECA, false, 0); });
    Save(party, "RetrySave");
    party.As(party.Host(), [&] { (void)host.PlayerLoads("RetrySave"); });
    party.Until([&] { return host.Loaded.size() == 1 && party.Host().GetTransport().IsOnline(); },
                "the host successfully loads a later choice");
    party.As(party.Guest(), [&] {
        REQUIRE(join.Join(std::format("127.0.0.1:{}", party.Host().GetHostService().GetNetwork()->GetPort())));
    });
    party.Until([&] { return party.Guest().GetTransport().IsOnline() && !join.IsJoining(); },
                "the guest rejoins through the successful save transfer");
    CHECK_FALSE(guest.References.at(0xECA).Locked);
    party.Agree("both games show the party in the successful world");
}

SCENARIO("A save the host makes goes ahead to the guests, and a load of it sends nothing", "[load][save-sync]")
{
    Party party(2, Plugins);
    auto& host = Party::Engine(party.Host());
    party.Agree("the party is together");
    // The save work done at once, or still running when the next update comes.
    const bool cDeferred = GENERATE(false, true);
    for (size_t i = 0; i < party.GameCount(); ++i)
        Party::Engine(i ? party.Guest(i - 1) : party.Host()).DefersWork = cDeferred;

    Quicksave(party, "Quicksave0");
    party.As(party.Host(), [&] { host.SetLock(0xECA, false, 0); });
    party.Until([&] { return !Party::Engine(party.Guest()).References.at(0xECA).Locked; }, "the world changes after the save");

    for (size_t round = 1; round <= 2; ++round)
    {
        party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads("Quicksave0")); });
        Reloads(party, round, "the whole party loads the save and rejoins");
        for (size_t i = 0; i < 2; ++i)
        {
            const auto& cMetrics = party.Guest(i).ctx().at<JoinService>().GetMetrics();
            CHECK(cMetrics.FromStore);
            CHECK(cMetrics.Chunks == 0);
            CHECK(cMetrics.PayloadBytes == 0);
            CHECK(cMetrics.WorldBytes > 0);
            CHECK(Party::Engine(party.Guest(i)).References.at(0xECA).Locked);
        }
        CHECK(host.References.at(0xECA).Locked);
    }
    party.Agree("every restored character has its copies");
}

SCENARIO("A load of a save the guest was never sent comes as its difference from one it holds", "[load][save-sync]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = party.Guest();
    party.Agree("the party is together");
    Quicksave(party, "Quicksave0");

    // A save from before the session: only on the host's disk, with the lock open.
    party.As(party.Host(), [&] {
        host.SetLock(0xECA, false, 0);
        host.SavedFiles["OldSave"] = host.MakeSave();
        host.Saves.push_back("OldSave");
        host.SetLock(0xECA, true, 0);
    });
    REQUIRE_FALSE(Holds(guest, ImageOf(party, "OldSave")));

    party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads("OldSave")); });
    Reloads(party, 1, "the party loads the old save");
    const auto cFirst = guest.ctx().at<JoinService>().GetMetrics();
    CHECK_FALSE(cFirst.FromStore);
    CHECK(cFirst.AgainstHeld);
    CHECK(cFirst.PayloadBytes > 0);
    CHECK_FALSE(Party::Engine(guest).References.at(0xECA).Locked);
    CHECK(Holds(guest, ImageOf(party, "OldSave")));

    // Loaded once, it is held: the next load of it sends nothing.
    party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads("OldSave")); });
    Reloads(party, 2, "the party loads the old save again");
    CHECK(guest.ctx().at<JoinService>().GetMetrics().FromStore);
    CHECK(guest.ctx().at<JoinService>().GetMetrics().PayloadBytes == 0);
}

SCENARIO("A guest told to load a save its store lost gets it sent", "[load][save-sync]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = party.Guest();
    party.Agree("the party is together");
    Quicksave(party, "Quicksave0");
    party.As(party.Host(), [&] { host.SetLock(0xECA, false, 0); });
    party.Until([&] { return !Party::Engine(guest).References.at(0xECA).Locked; }, "the world changes after the save");

    SECTION("its files are gone") { Party::Engine(guest).HeldSaveFiles.clear(); }
    SECTION("its file is damaged") { Party::Engine(guest).HeldSaveFiles.front().second = "damaged"; }

    party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads("Quicksave0")); });
    Reloads(party, 1, "the party loads the save, the guest's sent after all");
    const auto& cMetrics = guest.ctx().at<JoinService>().GetMetrics();
    CHECK_FALSE(cMetrics.FromStore);
    CHECK(cMetrics.PayloadBytes > 0);
    CHECK(Party::Engine(guest).References.at(0xECA).Locked);
    CHECK(Holds(guest, ImageOf(party, "Quicksave0")));
}

SCENARIO("A newer save replaces the one still on its way to a guest", "[save-sync]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = party.Guest();
    party.Agree("the party is together");

    // Saves of many pieces: one goes ahead a piece an update, behind the game's own messages.
    host.SavePadding = Noise(400 * 1024, 11);
    party.As(party.Host(), [&] { REQUIRE(host.Quicksaves("Quicksave0")); });
    for (int i = 0; i < 4; ++i)
        party.Frame();
    REQUIRE(host.SavedFiles.contains("Quicksave0"));
    REQUIRE(Party::Engine(guest).HeldSaves().empty());

    host.SavePadding = Noise(400 * 1024, 12);
    Quicksave(party, "Autosave1");
    CHECK_FALSE(Holds(guest, ImageOf(party, "Quicksave0")));
    CHECK(party.Host().GetTransport().IsOnline());
    CHECK(guest.GetTransport().IsOnline());

    // The one that never arrived still loads: it comes then.
    party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads("Quicksave0")); });
    Reloads(party, 1, "the party loads the first save");
    CHECK_FALSE(guest.ctx().at<JoinService>().GetMetrics().FromStore);
    CHECK(guest.ctx().at<JoinService>().GetMetrics().AgainstHeld);
}

SCENARIO("A guest whose held save is damaged still loads a save it was never sent", "[load][save-sync]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = party.Guest();
    auto& guestEngine = Party::Engine(guest);
    party.Agree("the party is together");
    Quicksave(party, "Quicksave0");
    party.As(party.Host(), [&] {
        host.SetLock(0xECA, false, 0);
        host.SavedFiles["OldSave"] = host.MakeSave();
        host.Saves.push_back("OldSave");
        host.SetLock(0xECA, true, 0);
    });
    REQUIRE(guestEngine.HeldSaveFiles.size() == 1);

    SECTION("damaged before the load") { guestEngine.HeldSaveFiles.front().second = "damaged"; }
    SECTION("damaged while the save is on its way against it")
    {
        // The host still encodes against the base it was told of; the guest loses it after the reload began.
        guestEngine.DefersWork = true;
    }
    party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads("OldSave")); });
    if (guestEngine.DefersWork)
    {
        party.Until([&] { return guest.ctx().at<JoinService>().IsJoining(); }, "the guest is told of the load");
        guestEngine.HeldSaveFiles.front().second = "damaged";
    }
    Reloads(party, 1, "the party loads the old save, the guest's sent whole");
    const auto& cMetrics = guest.ctx().at<JoinService>().GetMetrics();
    CHECK(cMetrics.Outcome == "joined");
    CHECK_FALSE(cMetrics.FromStore);
    CHECK_FALSE(cMetrics.AgainstHeld);
    CHECK_FALSE(guestEngine.References.at(0xECA).Locked);
    CHECK(Holds(guest, ImageOf(party, "OldSave")));
}

// The main menu's Continue loads the newest save of any character, and the Journal's Load list shows the current
// character's (save-game.md, "Continue"): a scratch save left behind is both.
SCENARIO("After a host load, Continue and the Load list keep to the chosen save's playthrough", "[load][saves]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    const std::string cOther = "Save3_0C0FFEE0_0_4C79646961_RiftenWorld_000050_20261101120000_1_1";
    const std::string cChosen = "Save12_0BADF00D_0_4C79646961_WhiterunWorld_000030_20261108213000_1_1";
    Save(party, cOther);
    Save(party, cChosen);
    // The guest's own game's save, from before it joined.
    guest.SavedFiles[std::string("Save1_0DEADBEE_0_4C79646961_Tamriel_000005_20261020100000_1_1")] = host.SavedFiles.at(cOther);

    party.As(party.Host(), [&] { CHECK_FALSE(host.PlayerLoads(cChosen)); });
    Reloads(party, 1, "the party loads the chosen save and rejoins");
    THEN("no game keeps a scratch save, and the host's list is the chosen character's")
    {
        for (const char* cpScratch : SaveNames::kScratch)
        {
            CHECK_FALSE(host.SavedFiles.contains(cpScratch));
            CHECK_FALSE(guest.SavedFiles.contains(cpScratch));
        }
        CHECK(host.LoadList() == std::vector<std::string>{cChosen});
    }

    WHEN("each player quits to the main menu and presses Continue")
    {
        for (auto* pEngine : {&host, &guest})
            pEngine->AtMainMenu = true;
        party.Frame();
        party.As(party.Host(), [&] { REQUIRE(host.Continues()); });
        party.As(party.Guest(), [&] { REQUIRE(guest.Continues()); });
        party.Until([&] { return host.Loaded.size() == 2 && guest.Loaded.size() == 2; }, "both games load the newest save");
        THEN("the host continues the chosen playthrough, the guest its own game")
        {
            CHECK(host.Loaded.back() == cChosen);
            CHECK(host.CharacterId == 0x0BADF00D);
            CHECK(host.LoadList() == std::vector<std::string>{cChosen});
            CHECK(guest.Loaded.back() == "Save1_0DEADBEE_0_4C79646961_Tamriel_000005_20261020100000_1_1");
            CHECK(guest.CharacterId == 0x0DEADBEE);
        }
    }
}

SCENARIO("A scratch save left by an earlier run is gone before the player can press Continue", "[load][saves]")
{
    Party party(0, Plugins);
    auto& host = Party::Engine(party.Host());
    const std::string cOwn = "Save2_0BADF00D_0_4C79646961_WhiterunWorld_000030_20261108213000_1_1";
    Save(party, cOwn);
    for (const char* cpScratch : SaveNames::kScratch)
        host.WritesSaveFile(cpScratch, host.SavedFiles.at(cOwn));
    host.AtMainMenu = true;
    party.Frame();
    for (const char* cpScratch : SaveNames::kScratch)
        CHECK_FALSE(host.SavedFiles.contains(cpScratch));
    party.As(party.Host(), [&] { REQUIRE(host.Continues()); });
    party.Until([&] { return host.Loaded.size() == 1; }, "Continue loads the newest save");
    CHECK(host.Loaded.back() == cOwn);
}
