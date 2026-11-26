// Party items (docs/design/party-items.md): the host's player's party stacks are the party's counts; any player's
// change of one changes them, and every game hears the count now.
#include "session.h"

#include <items/PartyItems.h>

#include <Messages/ContainerTransferRequest.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/RequestInventoryChanges.h>

namespace
{
const GameId kSword{0, 0x12EB7};

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

RequestInventoryChanges Sword(uint32_t aCharacter, int32_t aCount, bool aNew = false, const char* acpName = "[P] Iron Sword")
{
    RequestInventoryChanges change;
    change.SessionId = aCharacter;
    change.Item.BaseId = kSword;
    change.Item.Count = aCount;
    change.Item.ExtraName = acpName;
    change.NewPartyItem = aNew;
    return change;
}

const NotifyPartyItem* Heard(Session& aSession, ConnectionId_t aGame)
{
    const auto cSent = aSession.SentTo<NotifyPartyItem>(aGame);
    return cSent.empty() ? nullptr : cSent.back();
}
} // namespace

TEST_CASE("A guest's new party item is the party's: the host's player carries it and every game hears it", "[HostAuthority][PartyItems]")
{
    Session session;
    const auto cPlayers = Players(session);

    session.Receive(kPeer, Sword(cPlayers.at(kPeer), 1, true));
    CHECK(session.PartyItems[{kSword, "[P] Iron Sword"}] == 1);
    for (const ConnectionId_t cGame : {kHost, kPeer, kOtherPeer})
    {
        REQUIRE(Heard(session, cGame));
        CHECK(Heard(session, cGame)->Item.Count == 1);
        CHECK(Heard(session, cGame)->Item.ExtraName == "[P] Iron Sword");
    }
    CHECK(Heard(session, kPeer)->Shown == 0);
    CHECK(Heard(session, kOtherPeer)->Shown == 1);
    CHECK(session.SentTo<NotifyInventoryChanges>(kHost).empty()); // not relayed as an item
    const auto cLines = session.SentTo<NotifyChatMessageBroadcast>(kOtherPeer);
    REQUIRE(cLines.size() == 1);
    CHECK(cLines[0]->ChatMessage == "Lydia found Iron Sword for the party");

    SECTION("dropping it takes it from everyone, and says nothing")
    {
        session.Outbox.clear();
        session.Receive(kPeer, Sword(cPlayers.at(kPeer), -1));
        CHECK(session.PartyItems[{kSword, "[P] Iron Sword"}] == 0);
        CHECK(Heard(session, kHost)->Item.Count == 0);
        CHECK(session.SentTo<NotifyChatMessageBroadcast>(kHost).empty());
    }
}

TEST_CASE("The host's own change of a party stack is in its inventory already, its gift too; the gift is told as received", "[HostAuthority][PartyItems]")
{
    Session session;
    const auto cPlayers = Players(session);
    session.PartyItems[{kSword, "[P] Iron Sword"}] = 1; // the host's player dropped 1 of 2: its engine already took it

    session.Receive(kHost, Sword(cPlayers.at(kHost), -1));
    CHECK(session.PartyItems[{kSword, "[P] Iron Sword"}] == 1);
    CHECK(Heard(session, kPeer)->Item.Count == 1);

    // The host's engine renamed its gift where it is (shared-items.md): its player carries it already.
    auto gift = Sword(cPlayers.at(kHost), 1, true);
    gift.StoryGiven = true;
    session.PartyItems[{kSword, "[P] Iron Sword"}] = 2;
    session.Outbox.clear();
    session.Receive(kHost, gift);
    CHECK(session.PartyItems[{kSword, "[P] Iron Sword"}] == 2);
    CHECK(session.SentTo<NotifyChatMessageBroadcast>(kPeer).at(0)->ChatMessage == "The party received Iron Sword");
}

TEST_CASE("The story dressing the host's player: every game's player wears its copy", "[HostAuthority][PartyItems]")
{
    Session session;
    const auto cPlayers = Players(session);
    auto dressed = Sword(cPlayers.at(kHost), 1, true);
    dressed.StoryGiven = dressed.Wear = true;
    session.PartyItems[{kSword, "[P] Iron Sword"}] = 1;
    session.Receive(kHost, dressed);
    for (const ConnectionId_t cGame : {kHost, kPeer, kOtherPeer})
    {
        REQUIRE(Heard(session, cGame));
        CHECK(Heard(session, cGame)->Wear);
    }

    SECTION("a later change of it is no new dressing")
    {
        session.Outbox.clear();
        session.Receive(kHost, Sword(cPlayers.at(kHost), -1));
        CHECK_FALSE(Heard(session, kPeer)->Wear);
    }
}

TEST_CASE("A guest cannot change another player's party items; loot is no party item", "[HostAuthority][PartyItems]")
{
    Session session;
    const auto cPlayers = Players(session);
    session.Receive(kPeer, Sword(cPlayers.at(kOtherPeer), 1, true));
    CHECK(session.PartyItems[{kSword, "[P] Iron Sword"}] == 0);
    CHECK(session.SentTo<NotifyPartyItem>(kHost).empty());

    session.Receive(kPeer, Sword(cPlayers.at(kPeer), 1, false, ""));
    CHECK(session.SentTo<NotifyPartyItem>(kHost).empty());
}

TEST_CASE("A guest entering hears the party's whole list of items, on every game", "[HostAuthority][PartyItems]")
{
    Session session;
    session.PartyItems[{kSword, "[P] Iron Sword"}] = 2;
    session.PartyItems[{GameId(0, 0x93B08), "[P] Key to Breezehome"}] = 0; // the party had it and dropped it
    for (const ConnectionId_t cPlayer : {kHost, kOtherPeer, kPeer}) // kPeer enters last
    {
        session.Outbox.clear();
        AnnounceCharacterRequest announce;
        announce.SessionId = session.CharacterId(cPlayer);
        announce.ReferenceId = GameId(0, 0x14);
        session.Receive(cPlayer, announce);
    }

    for (const ConnectionId_t cGame : {kHost, kPeer, kOtherPeer})
    {
        const auto cSent = session.SentTo<NotifyPartyItems>(cGame);
        REQUIRE(cSent.size() == 1);
        REQUIRE(cSent.back()->Items.size() == 1);
        CHECK(cSent.back()->Items[0].BaseId == kSword);
        CHECK(cSent.back()->Items[0].Count == 2);
    }
}

TEST_CASE("A guest's take of a party stack from a host chest is the party's", "[HostAuthority][PartyItems]")
{
    Session session;
    const auto cPlayers = Players(session);
    ContainerTransferRequest take;
    take.Container = GameId(0, 0xC2CE8);
    take.Item = Sword(cPlayers.at(kPeer), 1).Item;
    session.Receive(kPeer, take);
    CHECK(session.PartyItems[{kSword, "[P] Iron Sword"}] == 1); // the host's player carries it
    REQUIRE(Heard(session, kOtherPeer));
    CHECK(Heard(session, kOtherPeer)->Item.Count == 1);
    CHECK(Heard(session, kOtherPeer)->Shown == 1);
    REQUIRE(Heard(session, kPeer));
    CHECK(Heard(session, kPeer)->Shown == 0); // its engine showed its own take

    SECTION("and storing it again is the party's too")
    {
        take.Item.Count = -1;
        session.Receive(kPeer, take);
        CHECK(session.PartyItems[{kSword, "[P] Iron Sword"}] == 0);
        CHECK(Heard(session, kOtherPeer)->Item.Count == 0);
    }
}

TEST_CASE("A guest's take of a quest alias's item the host's player got sends no grant; chat names the guest", "[HostAuthority][PartyItems]")
{
    // HostEngine::MoveAliasStack moved the Golden Claw to the host's player with its alias: its arrival there is the
    // party's (InventoryService); the guest gets its copy as a party item, not a grant of its own.
    Session session;
    const auto cPlayers = Players(session);
    const GameId cClaw(0, 0x39647);
    // The host's player's arrival reaches the party's items inside the move (the host's local route).
    session.Transferred = [&](const ContainerTransferRequest& acRequest) {
        Inventory::Entry moved = acRequest.Item;
        moved.IsQuestItem = true;
        RequestInventoryChanges arrival;
        arrival.SessionId = cPlayers.at(kHost);
        arrival.Item = moved;
        arrival.NewPartyItem = true;
        session.Receive(kHost, arrival);
        return moved;
    };
    ContainerTransferRequest take;
    take.Container = GameId(0, 0xC2CE8);
    take.Item.BaseId = cClaw;
    take.Item.Count = 1;
    session.Receive(kPeer, take);
    CHECK(session.SentTo<NotifyInventoryChanges>(kPeer).empty());
    const auto cLines = session.SentTo<NotifyChatMessageBroadcast>(kOtherPeer);
    REQUIRE(cLines.size() == 1);
    CHECK(cLines[0]->ChatMessage == "Lydia found Iron Sword for the party"); // the session's ItemName is the sword's
}

TEST_CASE("A guest's plain key arrival: the host decides it is the party's and tells the finder to rename its own", "[HostAuthority][PartyItems]")
{
    Session session;
    const auto cPlayers = Players(session);
    session.FormTypes[GameId(0, 0x93B08)] = PartyItems::cKeyFormType;
    RequestInventoryChanges found;
    found.SessionId = cPlayers.at(kPeer);
    found.Item.BaseId = GameId(0, 0x93B08);
    found.Item.Count = 1;
    session.Receive(kPeer, found);
    CHECK(session.PartyItems[{GameId(0, 0x93B08), "[P] Iron Sword"}] == 1); // the session names every item the sword
    REQUIRE(Heard(session, kPeer));
    CHECK(Heard(session, kPeer)->Converted == 1);
    CHECK(Heard(session, kPeer)->Shown == 0);
    REQUIRE(Heard(session, kOtherPeer));
    CHECK(Heard(session, kOtherPeer)->Converted == 0);
    CHECK(Heard(session, kOtherPeer)->Shown == 1);

    SECTION("an ordinary item stays the finder's")
    {
        session.Outbox.clear();
        RequestInventoryChanges loot = found;
        loot.Item.BaseId = GameId(0, 0x12EB7);
        session.Receive(kPeer, loot);
        CHECK_FALSE(Heard(session, kPeer));
    }
}
