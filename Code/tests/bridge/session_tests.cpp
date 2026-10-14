// HostSession: who gets into the session (docs/design/join-flow.md).
#include "session.h"

#include <Messages/AuthenticationResponse.h>
#include <Services/Host/JoinRefusal.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Structs/LoadOrder.h>

#include <map>
#include <tuple>

namespace
{
constexpr ConnectionId_t kNewcomer = 4;

AuthenticationRequest Join(const char* acpName)
{
    return JoinRequest(acpName);
}

Mods Plugins(std::initializer_list<std::pair<const char*, bool>> aPlugins)
{
    Mods plugins;
    for (const auto& [cpName, cLight] : aPlugins)
        plugins.ModList.push_back({cpName, 0, cLight});
    return plugins;
}

JoinRefusal::Install Ours(Mods aPlugins = HostPlugins())
{
    return {"v1.0.0-0-gdeadbeef", kHostGame, std::move(aPlugins)};
}

// The system lines the host's chat got.
std::vector<std::string> HostChat(const Session& acSession)
{
    std::vector<std::string> lines;
    for (const auto* cpLine : acSession.SentTo<NotifyChatMessageBroadcast>(kHost))
        if (cpLine->MessageType == kSystemMessage)
            lines.push_back(cpLine->ChatMessage.c_str());
    return lines;
}

const AuthenticationResponse& OnlyResponse(const Session& acSession, ConnectionId_t aTo)
{
    const auto cResponses = acSession.SentTo<AuthenticationResponse>(aTo);
    REQUIRE(cResponses.size() == 1);
    return *cResponses[0];
}
} // namespace

TEST_CASE("Join: accepted with a player id, the session's settings and the session's mod ids", "[Bridge][Session]")
{
    Session session;
    session.Receive(kNewcomer, Join("Serana"));

    const auto& cResponse = OnlyResponse(session, kNewcomer);
    CHECK(cResponse.Type == AuthenticationResponse::ResponseType::kAccepted);
    CHECK(cResponse.Version == BuildCommit());
    // A new id in the player list; the server keeps its own record for its last services.
    CHECK(cResponse.PlayerId != 0);
    for (const auto cPlayer : {kHost, kPeer, kOtherPeer})
        CHECK(cResponse.PlayerId != session.PlayerId(cPlayer));
    REQUIRE(session.World().GetPlayerManager().GetByConnectionId(kNewcomer));

    CHECK(cResponse.Settings.Difficulty == 3); // the host's
    CHECK(cResponse.Settings.DeathSystemEnabled);
    CHECK_FALSE(cResponse.Settings.PvpEnabled);
    CHECK_FALSE(cResponse.Settings.GreetingsEnabled);

    REQUIRE(cResponse.UserMods.ModList.size() == 3);
    CHECK(cResponse.UserMods.ModList[0].Filename == "Skyrim.esm");
    CHECK_FALSE(cResponse.UserMods.ModList[0].IsLite);
    CHECK(cResponse.UserMods.ModList[2].Filename == "ccBGSSSE001-Fish.esm");
    CHECK(cResponse.UserMods.ModList[2].IsLite);
    CHECK(cResponse.UserMods.ModList[0].Id != cResponse.UserMods.ModList[1].Id);

    // The others learn the newcomer.
    CHECK(session.SentTo<NotifyPlayerJoined>(kHost).size() == 1);
    CHECK(HostChat(session).empty());
}

TEST_CASE("Join: a game on another build is refused, told the host's build, and is not a player", "[Bridge][Session]")
{
    Session session;
    auto join = Join("Serana");
    join.Version = "v1.0.0-0-gdeadbeef";
    session.Receive(kNewcomer, join);

    const auto& cResponse = OnlyResponse(session, kNewcomer);
    CHECK(cResponse.Type == AuthenticationResponse::ResponseType::kWrongVersion);
    CHECK(cResponse.Version == BuildCommit());
    CHECK(session.World().GetPlayerManager().GetByConnectionId(kNewcomer) == nullptr);
    CHECK(session.SentTo<NotifyPlayerJoined>(kHost).empty());

    // The host's chat says who could not join and why; nobody else hears of it.
    CHECK(HostChat(session) == std::vector<std::string>{fmt::format("Serana couldn't join: they run SkyrimCoop v1.0.0-0-gdeadbeef, you run {}.", BuildCommit())});
    CHECK(session.SentTo<NotifyChatMessageBroadcast>(kPeer).empty());
}

TEST_CASE("Join: a game on another Skyrim or SKSE version is refused and told the host's", "[Bridge][Session]")
{
    auto join = Join("Serana");
    SECTION("Skyrim") { join.Game.Runtime = GameVersions::Pack(1, 6, 640); }
    SECTION("SKSE") { join.Game.Skse = GameVersions::Pack(2, 2, 3); }

    Session session;
    session.Receive(kNewcomer, join);
    const auto& cResponse = OnlyResponse(session, kNewcomer);
    CHECK(cResponse.Type == AuthenticationResponse::ResponseType::kWrongGame);
    CHECK(cResponse.Game == kHostGame);
    CHECK(session.World().GetPlayerManager().GetByConnectionId(kNewcomer) == nullptr);
    REQUIRE(HostChat(session).size() == 1);
    CHECK(HostChat(session)[0].starts_with("Serana couldn't join: they run Skyrim 1.6."));
}

TEST_CASE("Join: the build is checked before the game", "[Bridge][Session]")
{
    Session session;
    auto join = Join("Serana");
    join.Version = "v1.0.0-0-gdeadbeef";
    join.Game = {};
    session.Receive(kNewcomer, join);
    CHECK(OnlyResponse(session, kNewcomer).Type == AuthenticationResponse::ResponseType::kWrongVersion);
}

TEST_CASE("Join: a game with other plugins is refused and sent the host's", "[Bridge][Session]")
{
    Session session;
    auto join = Join("Serana");
    join.UserMods.ModList.push_back({"SkyUI_SE.esp", 3});
    session.Receive(kNewcomer, join);

    const auto& cResponse = OnlyResponse(session, kNewcomer);
    CHECK(cResponse.Type == AuthenticationResponse::ResponseType::kModsMismatch);
    CHECK(cResponse.UserMods == HostPlugins());
    CHECK(session.World().GetPlayerManager().GetByConnectionId(kNewcomer) == nullptr);
    CHECK(HostChat(session) == std::vector<std::string>{"Serana couldn't join: their plugins differ from yours (1 difference)."});
}

TEST_CASE("Load order: what differs between the host's plugins and a player's", "[Bridge][Session]")
{
    using enum LoadOrderDifference::Kind;
    const auto cHost = Plugins({{"Skyrim.esm", false}, {"Update.esm", false}, {"A.esp", false}, {"B.esp", false}, {"Fish.esm", true}});

    SECTION("the same plugins in the same order, whatever the case of their names")
    {
        CHECK(CompareLoadOrders(cHost, Plugins({{"skyrim.esm", false}, {"UPDATE.ESM", false}, {"A.esp", false}, {"B.esp", false}, {"fish.esm", true}})).empty());
    }
    SECTION("missing and extra")
    {
        const auto cDifferences = CompareLoadOrders(cHost, Plugins({{"Skyrim.esm", false}, {"Update.esm", false}, {"A.esp", false}, {"C.esp", false}, {"Fish.esm", true}}));
        CHECK(cDifferences == std::vector<LoadOrderDifference>{{kMissing, "B.esp"}, {kExtra, "C.esp"}});
    }
    SECTION("a light plugin loaded as a full one")
    {
        const auto cDifferences = CompareLoadOrders(cHost, Plugins({{"Skyrim.esm", false}, {"Update.esm", false}, {"A.esp", false}, {"B.esp", false}, {"Fish.esm", false}}));
        CHECK(cDifferences == std::vector<LoadOrderDifference>{{kLight, "Fish.esm", {}, {}, true}});
    }
    SECTION("the first place the order parts, once")
    {
        const auto cDifferences = CompareLoadOrders(cHost, Plugins({{"Skyrim.esm", false}, {"Update.esm", false}, {"B.esp", false}, {"A.esp", false}, {"Fish.esm", true}}));
        CHECK(cDifferences == std::vector<LoadOrderDifference>{{kOrder, "A.esp", "B.esp", "Update.esm"}});
    }
}

TEST_CASE("Join: a full session refuses the next game", "[Bridge][Session]")
{
    Session session;
    // Three are in; five more fill it.
    for (ConnectionId_t cConnection = 10; cConnection < 10 + HostSession::cMaxPlayers - 3; ++cConnection)
        session.Join(cConnection, Join("Guard"));
    session.Outbox.clear();

    session.Receive(kNewcomer, Join("Serana"));
    CHECK(OnlyResponse(session, kNewcomer).Type == AuthenticationResponse::ResponseType::kServerFull);
    CHECK(session.World().GetPlayerManager().GetByConnectionId(kNewcomer) == nullptr);
    CHECK(session.SentTo<NotifyPlayerJoined>(kHost).empty());
    CHECK(HostChat(session) == std::vector<std::string>{"Serana couldn't join: the session is full."});
}

TEST_CASE("Join: a player cannot join again", "[Bridge][Session]")
{
    Session session;
    const uint32_t cCount = session.World().GetPlayerManager().Count();
    session.Receive(kPeer, Join("Lydia"));
    CHECK(session.Outbox.empty());
    CHECK(session.World().GetPlayerManager().Count() == cCount);
}

TEST_CASE("Join: a game that is not a player can only ask to join", "[Bridge][Session]")
{
    Session session;
    SendChatMessageRequest chat;
    chat.MessageType = kGlobalChat;
    chat.ChatMessage = "hello?";
    session.Receive(kNewcomer, chat);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Join: a refused game's status has the reason's code and words", "[Bridge][Session]")
{
    using enum AuthenticationResponse::ResponseType;
    AuthenticationResponse refusal;
    refusal.Version = "v1.0.0-3-gcafe";

    refusal.Type = kWrongVersion;
    auto status = JoinRefusal::ForPlayer(refusal, Ours());
    CHECK(status.Current == JoinStatus::State::kRefused);
    CHECK(status.Reason == "wrong_build");
    CHECK(status.Message == "The host runs SkyrimCoop v1.0.0-3-gcafe, you run v1.0.0-0-gdeadbeef. You both need the same build.");

    refusal.Type = kWrongGame;
    refusal.Game = {GameVersions::Pack(1, 6, 640), GameVersions::Pack(2, 2, 3)};
    status = JoinRefusal::ForPlayer(refusal, Ours());
    CHECK(status.Reason == "wrong_game");
    CHECK(status.Message == "The host runs Skyrim 1.6.640 with SKSE 2.2.3, you run Skyrim 1.6.1170 with SKSE 2.2.6. You both need the same versions.");

    refusal.Type = kModsMismatch;
    refusal.UserMods = Plugins({{"Skyrim.esm", false}, {"Update.esm", false}, {"USSEP.esp", false}, {"A.esp", false}, {"B.esp", false}, {"Fish.esm", true}});
    status = JoinRefusal::ForPlayer(refusal, Ours(Plugins({{"Skyrim.esm", false}, {"Update.esm", false}, {"B.esp", false}, {"A.esp", false},
                                                          {"SkyUI_SE.esp", false}, {"Fish.esm", false}})));
    CHECK(status.Reason == "mods_differ");
    CHECK(status.Message == "Your plugins differ from the host's (4 differences). You both need the same plugins in the same order.");
    CHECK(status.Details == std::vector<std::string>{"missing USSEP.esp", "light Fish.esm: the host loads it as a light plugin, you as a full one",
                                                     "extra SkyUI_SE.esp", "order: after Update.esm the host loads A.esp, you load B.esp"});

    refusal.Type = kServerFull;
    status = JoinRefusal::ForPlayer(refusal, Ours());
    CHECK(status.Reason == "full");
    CHECK(status.Message == "The session is full.");

    // The host's line, and a game that sent no name.
    AuthenticationRequest join;
    join.Game = {GameVersions::Pack(1, 6, 640), GameVersions::Pack(2, 2, 3)};
    CHECK(JoinRefusal::ForHost(kWrongGame, join, Ours()) ==
          "A player couldn't join: they run Skyrim 1.6.640 with SKSE 2.2.3, you run Skyrim 1.6.1170 with SKSE 2.2.6.");
    CHECK(JoinRefusal::ForHost(kModsMismatch, join, Ours()) == "A player couldn't join: their plugins differ from yours (3 differences).");
}

TEST_CASE("Settings: the host's difficulty reaches the others when it changes", "[Bridge][Session]")
{
    Session session;
    session.Frame(1);
    CHECK(session.SentTo<NotifySettingsChange>(kPeer).empty()); // the joined have it already

    session.Difficulty = 5;
    session.Frame(2);
    for (const auto cPeer : {kPeer, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifySettingsChange>(cPeer);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->Settings.Difficulty == 5);
        CHECK(cNotifies[0]->Settings.DeathSystemEnabled);
    }
    CHECK(session.SentTo<NotifySettingsChange>(kHost).empty()); // its own

    // Joining later: the difficulty as it is now; nothing new while it stays.
    session.Outbox.clear();
    session.Frame(3);
    CHECK(session.SentTo<NotifySettingsChange>(kPeer).empty());
    session.Receive(4, JoinRequest("Serana"));
    const auto cResponses = session.SentTo<AuthenticationResponse>(4);
    REQUIRE(cResponses.size() == 1);
    CHECK(cResponses[0]->Settings.Difficulty == 5);
}

TEST_CASE("Player list: a newcomer learns everyone here, with the host marked, and they learn it", "[Bridge][Session]")
{
    Session session;
    const uint32_t cNewcomerId = session.Join(kNewcomer, Join("Serana"));

    // The newcomer: one each for the three already here.
    std::map<std::string, std::tuple<uint32_t, uint16_t, bool>> known;
    for (const auto* cpJoined : session.SentTo<NotifyPlayerJoined>(kNewcomer))
        known[cpJoined->Username.c_str()] = {cpJoined->PlayerId, cpJoined->Level, cpJoined->IsHost};
    CHECK(known.size() == 3);
    CHECK(known["Dovahkiin"] == std::tuple<uint32_t, uint16_t, bool>{session.PlayerId(kHost), 20, true});
    CHECK(known["Lydia"] == std::tuple<uint32_t, uint16_t, bool>{session.PlayerId(kPeer), 12, false});
    CHECK(known["Aela"] == std::tuple<uint32_t, uint16_t, bool>{session.PlayerId(kOtherPeer), 8, false});

    // Everyone else: the newcomer, not the host.
    for (const auto cPlayer : {kHost, kPeer, kOtherPeer})
    {
        const auto cAnnounced = session.SentTo<NotifyPlayerJoined>(cPlayer);
        REQUIRE(cAnnounced.size() == 1);
        CHECK(cAnnounced[0]->PlayerId == cNewcomerId);
        CHECK(cAnnounced[0]->Username == "Serana");
        CHECK(cAnnounced[0]->Level == 5);
        CHECK_FALSE(cAnnounced[0]->IsHost);
    }
}

TEST_CASE("Player list: the others drop a player who leaves; a refused game was never in it", "[Bridge][Session]")
{
    Session session;
    const uint32_t cPeerId = session.PlayerId(kPeer);
    session.Leave(kPeer);
    for (const auto cPlayer : {kHost, kOtherPeer})
    {
        const auto cLeft = session.SentTo<NotifyPlayerLeft>(cPlayer);
        REQUIRE(cLeft.size() == 1);
        CHECK(cLeft[0]->PlayerId == cPeerId);
        CHECK(cLeft[0]->Username == "Lydia");
    }
    CHECK(session.SentTo<NotifyPlayerLeft>(kPeer).empty());

    session.Outbox.clear();
    auto join = Join("Intruder");
    join.Version = "v1.0.0-0-gdeadbeef";
    session.Receive(kNewcomer, join);
    session.Leave(kNewcomer);
    CHECK(session.SentTo<NotifyPlayerLeft>(kHost).empty());
}

TEST_CASE("Player list: a player who leaves and joins again gets a new id", "[Bridge][Session]")
{
    Session session;
    const uint32_t cFirst = session.PlayerId(kPeer);
    session.Leave(kPeer);
    session.Outbox.clear();
    const uint32_t cAgain = session.Join(kPeer, Join("Lydia"));
    CHECK(cAgain != cFirst);
    for (const auto cPlayer : {kHost, kOtherPeer})
        CHECK(cAgain != session.PlayerId(cPlayer));
    // The others learn it under its new id.
    const auto cAnnounced = session.SentTo<NotifyPlayerJoined>(kHost);
    REQUIRE(cAnnounced.size() == 1);
    CHECK(cAnnounced[0]->PlayerId == cAgain);
}
