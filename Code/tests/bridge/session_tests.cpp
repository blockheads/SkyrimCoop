// HostSession: who gets into the session (docs/design/join-flow.md).
#include "session.h"

#include <Messages/AuthenticationResponse.h>
#include <Services/Host/JoinRefusal.h>

namespace
{
constexpr ConnectionId_t kNewcomer = 4;

AuthenticationRequest Join(const char* acpName)
{
    AuthenticationRequest join;
    join.Version = BuildCommit();
    join.Username = acpName;
    join.Level = 5;
    return join;
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
    auto join = Join("Serana");
    Mods::Entry skyrim;
    skyrim.Filename = "Skyrim.esm";
    join.UserMods.ModList.push_back(skyrim);
    Mods::Entry light;
    light.Filename = "ccBGSSSE001-Fish.esm";
    light.IsLite = true;
    join.UserMods.ModList.push_back(light);
    session.Receive(kNewcomer, join);

    const auto& cResponse = OnlyResponse(session, kNewcomer);
    CHECK(cResponse.Type == AuthenticationResponse::ResponseType::kAccepted);
    CHECK(cResponse.Version == BuildCommit());
    const auto* cpPlayer = session.World().GetPlayerManager().GetByConnectionId(kNewcomer);
    REQUIRE(cpPlayer);
    CHECK(cResponse.PlayerId == cpPlayer->GetId());
    CHECK(cpPlayer->GetUsername() == "Serana");
    CHECK(cpPlayer->GetLevel() == 5);

    CHECK(cResponse.Settings.Difficulty == 4);
    CHECK(cResponse.Settings.DeathSystemEnabled);
    CHECK_FALSE(cResponse.Settings.PvpEnabled);
    CHECK_FALSE(cResponse.Settings.GreetingsEnabled);

    REQUIRE(cResponse.UserMods.ModList.size() == 2);
    CHECK(cResponse.UserMods.ModList[0].Filename == "Skyrim.esm");
    CHECK_FALSE(cResponse.UserMods.ModList[0].IsLite);
    CHECK(cResponse.UserMods.ModList[1].Filename == "ccBGSSSE001-Fish.esm");
    CHECK(cResponse.UserMods.ModList[1].IsLite);
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

TEST_CASE("Join: a full session refuses the next game", "[Bridge][Session]")
{
    Session session;
    auto& players = session.World().GetPlayerManager();
    for (ConnectionId_t cConnection = 10; players.Count() < HostSession::cMaxPlayers; ++cConnection)
        players.Create(cConnection);

    session.Receive(kNewcomer, Join("Serana"));
    CHECK(OnlyResponse(session, kNewcomer).Type == AuthenticationResponse::ResponseType::kServerFull);
    CHECK(players.GetByConnectionId(kNewcomer) == nullptr);
    CHECK(players.Count() == HostSession::cMaxPlayers);
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
    auto status = JoinRefusal::ForPlayer(refusal, "v1.0.0-0-gdeadbeef");
    CHECK(status.Current == JoinStatus::State::kRefused);
    CHECK(status.Reason == "wrong_build");
    CHECK(status.Message == "The host runs SkyrimCoop v1.0.0-3-gcafe, you run v1.0.0-0-gdeadbeef. You both need the same build.");

    refusal.Type = kServerFull;
    status = JoinRefusal::ForPlayer(refusal, "v1.0.0-0-gdeadbeef");
    CHECK(status.Reason == "full");
    CHECK(status.Message == "The session is full.");

    // A game that sent no name.
    CHECK(JoinRefusal::ForHost(kServerFull, "", "", "") == "A player couldn't join: the session is full.");
}
