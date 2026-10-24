// Steam sessions (SteamService on FakeSteam; docs/design/join-flow.md, "Steam"): the host's game makes a lobby and
// invites a friend; the friend's player accepts, its game joins the lobby, connects to the lobby's owner, and joins
// the host's session through that connection (enet tunnelled over it).
#include "Party.h"

#include <Events/GameLoadedEvent.h>
#include <Events/SessionNoticeEvent.h>
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
        party.Until([&] { return State(guest).State == "failed"; }, "the friend's game loses the host");
        CHECK(State(guest).Error.starts_with("lost the host"));
        CHECK(!guest.GetTransport().IsOnline());
        // Its tunnel gone, the host's game drops the friend's session connection at once, not at enet's timeout.
        party.Until([&] { return cPlayerCopies() == 0; }, "the host's game drops the friend's player copy");
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
        party.Until([&] { return hostNotices.Has("Lydia joined."); }, "the host's player is told the friend joined");
    }

    WHEN("the friend leaves")
    {
        party.As(guest, [&] { Steam(guest).Leave(); });
        party.Until([&] { return hostNotices.Has("Lydia left."); }, "the host's player is told the friend left");
    }

    WHEN("the host's game loses Steam")
    {
        party.As(host, [&] { Steam(host).Leave(); });
        party.Until([&] { return State(guest).State == "failed"; }, "the friend's game loses the host");
        CHECK(guestNotices.Lines.back().starts_with("Left Dovahkiin's game: lost the host"));
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
