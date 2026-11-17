// The dev chat commands (docs/design/dev-commands.md): the host runs a world command, every game a player command.
#include "session.h"

namespace
{
void Announce(Session& aSession, ConnectionId_t aPlayer, uint32_t aReference)
{
    AnnounceCharacterRequest player;
    player.SessionId = aSession.CharacterId(aPlayer);
    player.ReferenceId = GameId(0, aReference);
    player.CellId = GameId(0, 0x32AE7);
    aSession.Receive(aPlayer, player);
}

DevCommandRequest Typed(const char* acpLine)
{
    DevCommandRequest request;
    request.Line = acpLine;
    return request;
}
} // namespace

TEST_CASE("Dev commands: a guest's spawn runs in the host's console at its character; every game hears of it",
          "[HostAuthority][DevTools]")
{
    Session session;
    Announce(session, kHost, 0x14);
    Announce(session, kPeer, 0x14);
    session.ConsoleNames[session.CharacterId(kHost)] = "player";
    session.ConsoleNames[session.CharacterId(kPeer)] = "FF000801";
    session.Outbox.clear();

    session.Receive(kPeer, Typed("/spawn wolf 3"));
    CHECK(session.ConsoleLines == std::vector<std::string>{"FF000801.placeatme B83C2 3"});
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifyDevCommand>(cTo);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->Line == "/spawn wolf 3");
        CHECK(cNotifies[0]->By == "Lydia");
    }

    session.ConsoleLines.clear();
    session.Receive(kHost, Typed("/weather snow"));
    CHECK(session.ConsoleLines == std::vector<std::string>{"fw 4D7FB"});
}

TEST_CASE("Dev commands: a player command runs nothing on the host's console; killall kills there once",
          "[HostAuthority][DevTools]")
{
    Session session;
    session.Receive(kPeer, Typed("/heal"));
    CHECK(session.ConsoleLines.empty());
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
        CHECK(session.SentTo<NotifyDevCommand>(cTo).size() == 1);

    session.Outbox.clear();
    session.Receive(kOtherPeer, Typed("/killall"));
    CHECK(session.HostilesKilled == 1);
    CHECK(session.ConsoleLines.empty());
    CHECK(session.SentTo<NotifyDevCommand>(kPeer).size() == 1);
}

TEST_CASE("Dev commands: a bad line, or a spawn at a character the host has not loaded, is dropped",
          "[HostAuthority][DevTools]")
{
    Session session;
    session.Receive(kPeer, Typed("/spawn unicorn"));
    session.Receive(kPeer, Typed("/tpa"));
    // No character announced: nowhere to place it.
    session.Receive(kPeer, Typed("/spawn wolf"));
    CHECK(session.ConsoleLines.empty());
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
        CHECK(session.SentTo<NotifyDevCommand>(cTo).empty());
}
