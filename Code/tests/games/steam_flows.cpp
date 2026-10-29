// Steam sessions (SteamService on FakeSteam; docs/design/join-flow.md, "Steam"): the host's game makes a lobby and
// invites a friend; the friend's player accepts, its game joins the lobby, connects to the lobby's owner, and joins
// the host's session through that connection (enet tunnelled over it).
#include "Party.h"

#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/NewGameEvent.h>
#include <Events/SessionNoticeEvent.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Services/SteamService.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

SteamService& Steam(World& aGame)
{
    return aGame.ctx().at<SteamService>();
}

const SteamService::Status& State(World& aGame)
{
    return Steam(aGame).GetStatus();
}

// The chat's system lines for the session (SessionNoticeEvent), as a game's player reads them.
struct Notices
{
    explicit Notices(World& aGame) { aGame.GetDispatcher().sink<SessionNoticeEvent>().connect<&Notices::On>(*this); }
    void On(const SessionNoticeEvent& acNotice) { Lines.push_back(acNotice.Text); }
    [[nodiscard]] bool Has(const std::string& acLine) const { return std::ranges::find(Lines, acLine) != Lines.end(); }
    std::vector<std::string> Lines;
};
} // namespace

SCENARIO("A friend accepts the host's Steam invite: its game joins the lobby and the host's session", "[steam]")
{
    Party party(1, Plugins, false);
    World& host = party.Host();
    World& guest = party.Guest();

    party.As(host, [&] { REQUIRE(Steam(host).Host()); });
    party.Until([&] { return State(host).State == "hosting"; }, "the host's game makes its lobby and listens");
    const auto cLobby = State(host).Lobby;

    party.As(host, [&] { REQUIRE(Steam(host).Invite(Party::Steam(guest).Id)); });
    THEN("Steam shows the friend the invite") CHECK(Party::Steam(guest).Invites.back().first == cLobby);

    Party::Steam(guest).ChooseJoin(cLobby);
    party.Until([&] { return State(guest).State == "connected"; }, "the friend's game joins the lobby and connects",
                [&] { return std::format("the friend's game {} ({})", State(guest).State, State(guest).Error); });
    THEN("each game knows the other")
    {
        CHECK(State(guest).Host == Party::Steam(host).Id);
        CHECK(State(guest).HostName == "Dovahkiin");
        party.Until([&] { return !State(host).Peers.empty() && State(host).Peers[0].Name == "Lydia"; },
                    "the host's game has the friend's hello");
    }

    party.Until([&] { return guest.GetTransport().IsOnline(); }, "the friend's game joins the session through Steam");
    FakeEngine& hostEngine = Party::Engine(host);
    const auto cPlayerCopies = [&] {
        size_t copies = 0;
        for (const uint32_t cId : hostEngine.CopiesOf())
            copies += hostEngine.Actors.at(cId).Player;
        return copies;
    };
    party.Until([&] { return cPlayerCopies() == 1; }, "the host's game makes a copy of the friend's player");
    party.Agree("each game has the other's player");

    WHEN("the host leaves")
    {
        party.As(host, [&] { Steam(host).Leave(); });
        // Its tunnel gone, the host's game drops the friend's session connection at once, not at enet's timeout.
        party.Until([&] { return cPlayerCopies() == 0; }, "the host's game drops the friend's player copy");
        party.Until([&] { return guest.ctx().at<JoinService>().GetStatus().Current == JoinStatus::State::kEnded; },
                    "the friend's game tries the lobby again until its tries run out", {}, nullptr,
                    std::chrono::seconds{static_cast<int64_t>(JoinService::kReconnectTries *
                        (JoinService::kReconnectSeconds + JoinService::kRetrySeconds) + 5)});
        CHECK(guest.ctx().at<JoinService>().GetStatus().Reason == "lost_connection");
        CHECK(!guest.GetTransport().IsOnline());
        CHECK(State(guest).State == "idle");
    }

    WHEN("the connection between them drops while the host's lobby stays")
    {
        party.As(host, [&] { Party::Steam(host).Drop(State(host).Peers[0].Connection); });
        party.Until([&] { return guest.ctx().at<JoinService>().GetStatus().Current == JoinStatus::State::kReconnecting; },
                    "the friend's game lost the host: it reconnects");
        party.Until([&] { return guest.GetTransport().IsOnline(); }, "the friend's game is back, through the same lobby",
                    [&] { return std::format("the friend's Steam {} ({})", State(guest).State, State(guest).Error); });
        party.Until([&] { return cPlayerCopies() == 1; }, "the host's game has the friend's player again");
        party.Agree("each game has the other's player again");
        CHECK(State(guest).Lobby == cLobby);
    }

    WHEN("the friend leaves")
    {
        party.As(guest, [&] { Steam(guest).Leave(); });
        party.Until([&] { return State(host).Peers.empty() && cPlayerCopies() == 0; },
                    "the host's game drops the friend and its player's copy");
    }
}

SCENARIO("A Steam lobby that is no SkyrimCoop session is refused", "[steam]")
{
    Party party(1, {}, false);
    World& guest = party.Guest();

    // Another game's lobby under Skyrim's App ID: no protocol in its data.
    FakeSteam stranger;
    REQUIRE(stranger.CreateLobby());
    stranger.PollLobbyRequest();
    const auto cLobby = stranger.PollLobbyRequest().Lobby;
    REQUIRE(cLobby);

    Party::Steam(guest).ChooseJoin(cLobby);
    party.Until([&] { return State(guest).State == "failed"; }, "the friend's game refuses the lobby");
    CHECK(State(guest).Error == "the lobby is not a SkyrimCoop session of this version");
}

SCENARIO("Each player loads a save and hosts; a friend accepts the host's invite and its game joins the host's", "[steam]")
{
    Party party(1, Plugins, false);
    World& host = party.Host();
    World& guest = party.Guest();
    Notices hostNotices(host), guestNotices(guest);

    // A save loads in each game (TESLoadGameEvent): each hosts on Steam by itself.
    party.As(host, [&] { host.GetDispatcher().trigger(GameLoadedEvent{}); });
    party.As(guest, [&] { guest.GetDispatcher().trigger(GameLoadedEvent{}); });
    party.Until([&] { return State(host).State == "hosting" && State(guest).State == "hosting"; }, "both games host");
    CHECK(guest.GetHostService().IsHosting());
    CHECK(guest.GetTransport().IsConnectedLocally());
    CHECK(hostNotices.Has("Hosting: friends can join from Steam (Shift+Tab, Invite to Game)."));

    party.As(host, [&] { REQUIRE(Steam(host).Invite(Party::Steam(guest).Id)); });
    Party::Steam(guest).ChooseJoin(State(host).Lobby);
    party.Until([&] { return guest.GetTransport().IsOnline() && State(guest).State == "connected"; },
                "the friend's game stops hosting and joins the host's session through Steam",
                [&] { return std::format("the friend's game {} ({})", State(guest).State, State(guest).Error); });
    THEN("the friend's game no longer hosts, and each player is told")
    {
        CHECK(!guest.GetHostService().IsHosting());
        CHECK(guestNotices.Has("Joined Dovahkiin's game."));
        party.Until([&] { return hostNotices.Has("Lydia joined the party."); }, "the host's player is told the friend joined");
    }

    WHEN("the friend leaves")
    {
        party.As(guest, [&] { Steam(guest).Leave(); });
        party.Until([&] { return hostNotices.Has("Lydia left the party."); }, "the host's player is told the friend left");
    }

    WHEN("the host's game loses Steam")
    {
        party.As(host, [&] { Steam(host).Leave(); });
        party.Until([&] { return guestNotices.Has("Lost connection to the host. Reconnecting..."); },
                    "the friend's player is told it lost the host");
        party.Until([&] { return guestNotices.Has("Couldn't reconnect to the host."); },
                    "the friend's player is told reconnecting failed (the lobby is gone)", {}, nullptr,
                    std::chrono::seconds{static_cast<int64_t>(JoinService::kReconnectTries *
                        (JoinService::kReconnectSeconds + JoinService::kRetrySeconds) + 5)});
    }
}

SCENARIO("A player starts a new game: it hosts once its world is up, and steps down to join a friend's", "[steam]")
{
    Party party(1, Plugins, false);
    World& host = party.Host();
    World& guest = party.Guest();
    party.As(host, [&] { REQUIRE(Steam(host).Host()); });
    party.Until([&] { return State(host).State == "hosting"; }, "the host's game hosts");

    // New at the main menu (NewGame_Enter), then its loading screen.
    party.As(guest, [&] {
        guest.GetDispatcher().trigger(NewGameEvent{});
        guest.GetDispatcher().trigger(MenuEvent{"Loading Menu", true});
    });
    party.Frame();
    THEN("not while it loads") CHECK(State(guest).State == "idle");
    party.As(guest, [&] { guest.GetDispatcher().trigger(MenuEvent{"Loading Menu", false}); });
    party.Until([&] { return State(guest).State == "hosting" && guest.GetHostService().IsHosting(); },
                "the new game's world is up: it hosts");

    WHEN("its player joins a friend's game from it (JoinService)")
    {
        party.As(guest, [&] {
            REQUIRE(guest.ctx().at<JoinService>().Join(std::format("127.0.0.1:{}", host.GetHostService().GetNetwork()->GetPort())));
        });
        THEN("it stopped hosting first") CHECK(!guest.GetHostService().IsHosting());
    }
}

SCENARIO("A host with friends in its game does not leave it for an invite", "[steam]")
{
    Party party(2, Plugins, false);
    World& host = party.Host();
    World& lydia = party.Guest(0);
    World& aela = party.Guest(1);
    Notices lydiaNotices(lydia);

    // Lydia hosts and Aela is in her game; the Dovahkiin hosts too and invites Lydia.
    for (World* pGame : {&host, &lydia})
        party.As(*pGame, [&] { pGame->GetDispatcher().trigger(GameLoadedEvent{}); });
    party.Until([&] { return State(host).State == "hosting" && State(lydia).State == "hosting"; }, "both host");
    Party::Steam(aela).ChooseJoin(State(lydia).Lobby);
    party.Until([&] { return aela.GetTransport().IsOnline(); }, "Aela joins Lydia's game");

    Party::Steam(lydia).ChooseJoin(State(host).Lobby);
    party.Until([&] { return lydiaNotices.Has("Couldn't join: friends are in your game. Leave it first."); },
                "Lydia's player is told why she stays");
    CHECK(State(lydia).State == "hosting");
    CHECK(lydia.GetHostService().IsHosting());
}

SCENARIO("A Steam invite at the main menu joins as a kept or newly created character", "[steam][creation]")
{
    Party party(1, Plugins, false);
    World& host = party.Host();
    World& guest = party.Guest();

    party.As(host, [&] { REQUIRE(Steam(host).Host()); });
    party.Until([&] { return State(host).State == "hosting"; }, "the host's game makes its lobby and listens");
    const bool launchedForInvite = GENERATE(false, true);
    if (launchedForInvite)
        Party::Steam(guest).LaunchLobby = State(host).Lobby;
    else
        Party::Steam(guest).ChooseJoin(State(host).Lobby);
    Party::Engine(guest).AtMainMenu = true;

    WHEN("the host's save keeps the friend's character")
    {
        PlayerRecord kept;
        kept.Key = std::format("steam:{}", Party::Steam(guest).Id);
        kept.Name = "Lydia";
        kept.Level = 17;
        party.As(host, [&] { host.ctx().at<SaveService>().GetRecords().Put(kept); });
        party.Until([&] { return guest.GetTransport().IsOnline() && !guest.ctx().at<JoinService>().IsJoining(); },
                    "the friend's game joins the lobby, loads the host's save and joins as its kept character",
                    [&] { return std::format("Steam {} ({}), join {}", State(guest).State, State(guest).Error,
                                             ToString(guest.ctx().at<JoinService>().GetStatus().Current)); });
        CHECK(Party::Engine(guest).Player.Level == 17);
        CHECK(Party::Engine(guest).Loaded == std::vector<std::string>{JoinService::kJoinSave});
    }

    WHEN("the host's save does not know the friend")
    {
        auto& engine = Party::Engine(guest);
        auto& join = guest.ctx().at<JoinService>();
        party.Until([&] { return join.IsCreatingCharacter(); }, "the launch invite opens character creation");
        const auto lobby = State(guest).Lobby;
        const auto key = std::format("steam:{}", Party::Steam(guest).Id);
        CHECK(join.PlayerKey() == key);
        CHECK(engine.Loaded.empty());
        engine.AtMainMenu = false;
        engine.Player.Name = "Rinara";
        engine.Player.Level = 1;
        const bool hostLost = GENERATE(false, true);
        if (hostLost)
        {
            engine.CreationPhase = IJoinBridge::Creation::kCustomizing;
            Party::Steam(guest).Drop(State(guest).Peers.front().Connection);
            party.Until([&] { return !join.IsJoining(); }, "Steam host loss clears the automatic join during customization");
            CHECK(join.GetStatus().Reason == "lost_connection");
            engine.CreationPhase = IJoinBridge::Creation::kComplete;
            party.Frame();
            CHECK(engine.Player.Name == "Rinara");
            CHECK(engine.Loaded.empty());
            return;
        }
        engine.CreationPhase = IJoinBridge::Creation::kComplete;
        party.Until([&] { return guest.GetTransport().IsOnline() && !join.IsJoining(); }, "naming resumes joining through the retained Steam tunnel");
        CHECK(State(guest).Lobby == lobby);
        CHECK(join.PlayerKey() == key);
        CHECK(engine.Player.Name == "Rinara");
        CHECK(engine.CreationRequests == 1);
        CHECK_FALSE(guest.GetHostService().IsHosting());
    }
}

TEST_CASE("Steam's +connect_lobby is read from the game's command line", "[steam]")
{
    CHECK(ISteamBridge::ParseConnectLobby(R"("C:\Games\Skyrim Special Edition\SkyrimSE.exe" +connect_lobby 109775241234567890)") ==
          109775241234567890ull);
    CHECK(ISteamBridge::ParseConnectLobby("skse64_loader.exe +connect_lobby  42 -windowed") == 42);
    CHECK(ISteamBridge::ParseConnectLobby("SkyrimSE.exe -windowed") == 0);
}
