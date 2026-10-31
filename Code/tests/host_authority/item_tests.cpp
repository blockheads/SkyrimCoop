// HostItems: a loose item exists once, in the host's world; a guest's drop is made there and a pickup is the host's
// to grant (docs/design/dropped-items.md).
#include "session.h"

#include <Messages/DroppedItems.h>

namespace
{
constexpr uint32_t kQaSmoke = 0x31D2E;

DropItemRequest Drop(uint32_t aTag, uint32_t aCell = kQaSmoke)
{
    DropItemRequest drop;
    drop.Tag = aTag;
    drop.Item.BaseId = GameId(0, 0x12EB7); // an iron sword
    drop.Item.Count = 1;
    drop.Item.ExtraHealth = 1.4f;
    drop.Item.ExtraName = "Old Friend";
    drop.Placement.Cell = GameId(0, aCell);
    drop.Placement.Position[0] = 30.f;
    return drop;
}

PickUpRequest PickUp(const GameId& acObject, uint32_t aTag = 0)
{
    PickUpRequest pickUp;
    pickUp.Object = acObject;
    pickUp.Tag = aTag;
    return pickUp;
}
} // namespace

TEST_CASE("Items: a guest's drop is made in the host's world, its game told which it is", "[HostAuthority][Items]")
{
    Session session;
    session.LoadedCells[kQaSmoke] = {};
    session.Receive(kPeer, Drop(0xFF000A31));

    REQUIRE(session.Loose.size() == 1);
    const auto& [cObject, cItem] = *session.Loose.begin();
    CHECK(cItem == Drop(0).Item);
    const auto cMade = session.SentTo<NotifyItemDropped>(kPeer);
    REQUIRE(cMade.size() == 1);
    CHECK(cMade[0]->Tag == 0xFF000A31);
    CHECK(cMade[0]->Object == cObject);
    THEN("nobody else is told: the creation goes out as the host's world's") CHECK(session.SentTo<NotifyItemDropped>(kOtherPeer).empty());
}

TEST_CASE("Items: a drop in a cell the host's game has not loaded waits for it", "[HostAuthority][Items]")
{
    Session session;
    session.Receive(kPeer, Drop(0xFF000A31));
    session.Frame(1000);
    CHECK(session.Loose.empty());
    CHECK(session.SentTo<NotifyItemDropped>(kPeer).empty());

    WHEN("the host's game loads it")
    {
        session.LoadedCells[kQaSmoke] = {};
        session.Frame(1001);
        CHECK(session.Loose.size() == 1);
        CHECK(session.SentTo<NotifyItemDropped>(kPeer).size() == 1);
    }
    WHEN("the dropper picks it up again first: it comes back, never made")
    {
        session.Receive(kPeer, PickUp({}, 0xFF000A31));
        const auto cGranted = session.SentTo<NotifyPickUp>(kPeer);
        REQUIRE(cGranted.size() == 1);
        CHECK(cGranted[0]->Tag == 0xFF000A31);
        CHECK(cGranted[0]->Item == Drop(0).Item);
        session.LoadedCells[kQaSmoke] = {};
        session.Frame(1001);
        CHECK(session.Loose.empty());
    }
    WHEN("the dropper leaves")
    {
        session.Leave(kPeer);
        session.LoadedCells[kQaSmoke] = {};
        session.Frame(1001);
        CHECK(session.Loose.empty());
    }
}

TEST_CASE("Items: a drop the host's game cannot make is forgotten, its dropper told", "[HostAuthority][Items]")
{
    Session session;
    session.LoadedCells[kQaSmoke] = {};
    DropItemRequest drop = Drop(0xFF000A31);
    drop.Item.BaseId = {}; // a base the host's game lacks
    session.Receive(kPeer, drop);
    session.Frame(1000);
    session.Frame(1001);
    CHECK(session.CreateCalls == 1);
    // Its own stays its own (an empty id): its player picks it up without asking.
    const auto cAnswers = session.SentTo<NotifyItemDropped>(kPeer);
    REQUIRE(cAnswers.size() == 1);
    CHECK(cAnswers[0]->Tag == 0xFF000A31);
    CHECK(cAnswers[0]->Object == GameId{});
}

TEST_CASE("Items: two players pick up one item, the first gets it", "[HostAuthority][Items]")
{
    Session session;
    session.LoadedCells[kQaSmoke] = {};
    session.Receive(kPeer, Drop(0xFF000A31));
    const GameId cObject = session.Loose.begin()->first;
    session.Outbox.clear();

    session.Receive(kOtherPeer, PickUp(cObject));
    session.Receive(kPeer, PickUp(cObject));

    const auto cFirst = session.SentTo<NotifyPickUp>(kOtherPeer);
    REQUIRE(cFirst.size() == 1);
    CHECK(cFirst[0]->Object == cObject);
    CHECK(cFirst[0]->Item == Drop(0).Item);
    CHECK(session.Loose.empty());
    THEN("the second gets nothing") CHECK(session.SentTo<NotifyPickUp>(kPeer).empty());
}

TEST_CASE("Items: a dropper asking for its drop by tag gets the host's made one", "[HostAuthority][Items]")
{
    Session session;
    session.LoadedCells[kQaSmoke] = {};
    session.Receive(kPeer, Drop(0xFF000A31));
    session.Outbox.clear();

    // Sent before the answer reached it: its tag only.
    session.Receive(kPeer, PickUp({}, 0xFF000A31));
    const auto cGranted = session.SentTo<NotifyPickUp>(kPeer);
    REQUIRE(cGranted.size() == 1);
    CHECK(cGranted[0]->Tag == 0xFF000A31);
    CHECK(cGranted[0]->Object != GameId{});
    CHECK(session.Loose.empty());
}

TEST_CASE("Items: the host's own drops and pickups are its world's", "[HostAuthority][Items]")
{
    Session session;
    session.LoadedCells[kQaSmoke] = {};
    session.Receive(kHost, Drop(0xFF000A31));
    CHECK(session.Loose.empty());
    session.Receive(kPeer, Drop(0xFF000A32));
    session.Receive(kHost, PickUp(session.Loose.begin()->first));
    CHECK(session.Loose.size() == 1);
}

TEST_CASE("Items: a guest's chest change carries its items' extra data", "[HostAuthority][Items]")
{
    Session session;
    ContainerChangeRequest items;
    items.Container = GameId(0, 0xA05B5);
    items.Item = GameId(0, 0x12EB7);
    items.Count = 3;
    items.Stacks.push_back(Drop(0).Item);
    session.Receive(kPeer, items);
    REQUIRE(session.ItemsSet.size() == 1);
    CHECK(session.ItemsSet[0].Type == ObjectChange::Kind::kItems);
    CHECK(session.ItemsSet[0].Value == 3);
    CHECK(session.ItemsSet[0].Stacks == items.Stacks);
}

TEST_CASE("Items: a guest picking up a quest object: the host's player takes it for the party, no grant", "[HostAuthority][Items][PartyItems]")
{
    Session session;
    session.LoadedCells[kQaSmoke] = {};
    const GameId cClaw(ObjectChange::kCreatedModId, 0x900);
    session.Loose[cClaw] = {};
    session.QuestObjects.insert(cClaw);
    session.Receive(kPeer, PickUp(cClaw));
    CHECK(session.TakenForParty == std::vector{cClaw});
    CHECK(session.SentTo<NotifyPickUp>(kPeer).empty());
    CHECK(session.Loose.empty());
}
