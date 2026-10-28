// Host loads reuse the wipe's transfer and rejoin; guest-initiated loads are refused in chat.
#include "Party.h"

#include <Events/GameLoadFailedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/SessionNoticeEvent.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>

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
