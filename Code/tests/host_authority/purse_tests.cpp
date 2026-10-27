// The party's gold (docs/design/party-purse.md): one purse, the host's player's gold; any player's change changes it,
// and every game hears what it is now.
#include "session.h"

#include <Messages/RequestInventoryChanges.h>

namespace
{
// Every player's character announced, the host's own too; its session id by owner.
std::map<ConnectionId_t, uint32_t> Players(Session& aSession)
{
    std::map<ConnectionId_t, uint32_t> characters;
    for (const ConnectionId_t cPlayer : {kHost, kPeer, kOtherPeer})
    {
        AnnounceCharacterRequest announce;
        announce.SessionId = aSession.CharacterId(cPlayer);
        announce.ReferenceId = GameId(0, 0x14);
        aSession.Receive(cPlayer, announce);
        characters[cPlayer] = announce.SessionId;
    }
    aSession.Outbox.clear();
    return characters;
}

RequestInventoryChanges Gold(uint32_t aCharacter, int32_t aCount)
{
    RequestInventoryChanges change;
    change.SessionId = aCharacter;
    change.Item.BaseId = Payloads::PartyGold::kGold;
    change.Item.Count = aCount;
    change.UpdateClients = true;
    return change;
}

// What each game heard the purse is now, or none.
std::optional<uint32_t> Heard(Session& aSession, ConnectionId_t aGame)
{
    const auto cSent = aSession.SentTo<NotifyPartyGold>(aGame);
    return cSent.empty() ? std::nullopt : std::optional(cSent.back()->Gold);
}
} // namespace

TEST_CASE("A guest's gold goes into the party's purse, and every game carries it", "[HostAuthority][Purse]")
{
    Session session;
    const auto cPlayers = Players(session);
    session.PartyGold = 100;

    session.Receive(kPeer, Gold(cPlayers.at(kPeer), 50));
    CHECK(session.PartyGold == 150);
    for (const ConnectionId_t cGame : {kHost, kPeer, kOtherPeer})
        CHECK(Heard(session, cGame) == 150u);
    CHECK(session.SentTo<NotifyInventoryChanges>(kHost).empty()); // not relayed as an item

    SECTION("a guest spends more than the party has: the purse is empty")
    {
        session.Receive(kOtherPeer, Gold(cPlayers.at(kOtherPeer), -400));
        CHECK(session.PartyGold == 0);
        CHECK(Heard(session, kPeer) == 0u);
    }
}

TEST_CASE("The host's own gold is the purse: its change is in already", "[HostAuthority][Purse]")
{
    Session session;
    const auto cPlayers = Players(session);
    session.PartyGold = 70; // the host's player spent 30 of 100

    session.Receive(kHost, Gold(cPlayers.at(kHost), -30));
    CHECK(session.PartyGold == 70);
    CHECK(Heard(session, kPeer) == 70u);
}

TEST_CASE("The host's script giving a guest's copy gold fills the purse", "[HostAuthority][Purse]")
{
    Session session;
    const auto cPlayers = Players(session);
    session.Receive(kHost, Gold(cPlayers.at(kPeer), 25)); // a quest reward on the guest's copy (rule 2)
    CHECK(session.PartyGold == 25);
    CHECK(Heard(session, kPeer) == 25u);
}

TEST_CASE("A guest cannot change another player's gold, and an NPC's gold is an item", "[HostAuthority][Purse]")
{
    Session session;
    const auto cPlayers = Players(session);
    session.Receive(kPeer, Gold(cPlayers.at(kOtherPeer), 1000));
    CHECK(session.PartyGold == 0);
    CHECK_FALSE(Heard(session, kHost));

    const uint32_t cNpc = session.Character(kHost);
    session.Receive(kHost, Gold(cNpc, 10));
    CHECK(session.PartyGold == 0);
    CHECK(session.SentTo<NotifyInventoryChanges>(kPeer).size() == 1);
}

TEST_CASE("A guest entering carries the party's gold", "[HostAuthority][Purse]")
{
    Session session;
    session.PartyGold = 640;
    AnnounceCharacterRequest announce;
    announce.SessionId = session.CharacterId(kHost);
    announce.ReferenceId = GameId(0, 0x14);
    session.Receive(kHost, announce);
    session.Outbox.clear();

    announce.SessionId = session.CharacterId(kPeer);
    session.Receive(kPeer, announce);
    CHECK(Heard(session, kPeer) == 640u);
    CHECK(Heard(session, kHost) == 640u); // the host's copy of it too
}
