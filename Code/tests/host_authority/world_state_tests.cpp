// HostWorldState: the world is the host's; what changes in its game reaches the others.
#include "session.h"

namespace
{
const GameId kFriendsCountRift(0, 0x87E1C);
const GameId kWEEnabled(0, 0xB8EC1);
const GameId kGameHour(0, 0x38);
const GameId kGameDaysPassed(0, 0x39);

float ValueOf(const NotifyGlobals& acNotify, const GameId& acId)
{
    for (const auto& cValue : acNotify.Values)
        if (cValue.Id == acId)
            return cValue.Value;
    FAIL("global not in the notification");
    return 0.f;
}
} // namespace

TEST_CASE("World state: a changed global reaches the others, not the host", "[HostAuthority][WorldState]")
{
    Session session;
    session.Globals = {{kFriendsCountRift, 0.f}, {kWEEnabled, 1.f}};
    session.Frame(1000);
    // The first look sends the whole table.
    REQUIRE(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyGlobals>(kPeer)[0]->Values.size() == 2);
    CHECK(session.SentTo<NotifyGlobals>(kHost).empty());

    session.Outbox.clear();
    session.Globals[0].Value = 1.f;
    session.GlobalChanges = {session.Globals[0]};
    session.Frame(1001);
    REQUIRE(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
    REQUIRE(session.SentTo<NotifyGlobals>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyGlobals>(kPeer)[0]->Values.size() == 1);
    CHECK(ValueOf(*session.SentTo<NotifyGlobals>(kPeer)[0], kFriendsCountRift) == 1.f);
    CHECK(session.SentTo<NotifyGlobals>(kHost).empty());
}

TEST_CASE("World state: the clock's globals are never sent", "[HostAuthority][WorldState]")
{
    Session session;
    session.Globals = {{kFriendsCountRift, 0.f}, {kGameHour, 10.f, true}, {kGameDaysPassed, 3.4f, true}};
    session.Frame(1000);
    REQUIRE(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyGlobals>(kPeer)[0]->Values.size() == 1);
    CHECK(ValueOf(*session.SentTo<NotifyGlobals>(kPeer)[0], kFriendsCountRift) == 0.f);

    // The clock running or jumping: HostClock's, not a global's change.
    session.Outbox.clear();
    session.Globals[1].Value = 16.f;
    session.Globals[2].Value = 4.6f;
    session.GlobalChanges = {session.Globals[1], session.Globals[2]};
    session.Frame(1001);
    CHECK(session.SentTo<NotifyGlobals>(kPeer).empty());

    // Nor to a joining player.
    AnnounceCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    announce.SessionId = session.CharacterId(kPeer);
    session.Receive(kPeer, announce);
    REQUIRE(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyGlobals>(kPeer)[0]->Values.size() == 1);
}

TEST_CASE("World state: an unchanged global is not sent again", "[HostAuthority][WorldState]")
{
    Session session;
    session.Globals = {{kFriendsCountRift, 0.f}, {kWEEnabled, std::numeric_limits<float>::quiet_NaN()}};
    session.Frame(1000);
    session.Outbox.clear();

    session.GlobalChanges = session.Globals;
    session.Frame(1001);
    session.Frame(100000);
    CHECK(session.GlobalReads == 1);
    CHECK(session.SentTo<NotifyGlobals>(kPeer).empty());
}

TEST_CASE("World state: global writes go out next frame without scanning the table", "[HostAuthority][WorldState]")
{
    Session session;
    session.Globals = {{kFriendsCountRift, 0.f}};
    session.Frame(1000);
    session.Outbox.clear();

    session.Globals[0].Value = -0.f;
    session.GlobalChanges = {session.Globals[0]};
    session.Frame(1001);
    REQUIRE(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
    CHECK(std::signbit(ValueOf(*session.SentTo<NotifyGlobals>(kPeer)[0], kFriendsCountRift)));
    session.Outbox.clear();
    session.Frame(100000);
    CHECK(session.SentTo<NotifyGlobals>(kPeer).empty());
    CHECK(session.GlobalReads == 1);
}

TEST_CASE("World state: globals written after an empty initial table reach guests", "[HostAuthority][WorldState]")
{
    Session session;
    session.Frame(1000);
    session.GlobalChanges = {{kFriendsCountRift, 2.f}};
    session.Frame(1001);
    REQUIRE(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
    CHECK(ValueOf(*session.SentTo<NotifyGlobals>(kPeer)[0], kFriendsCountRift) == 2.f);
    CHECK(session.GlobalReads == 1);
}

TEST_CASE("World state: a joining player gets every global", "[HostAuthority][WorldState]")
{
    Session session;
    session.Globals = {{kFriendsCountRift, 3.f}, {kWEEnabled, 1.f}};
    session.Frame(1000);
    session.Outbox.clear();

    AnnounceCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    announce.SessionId = session.CharacterId(kPeer);
    session.Receive(kPeer, announce);
    REQUIRE(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
    const auto& cAll = *session.SentTo<NotifyGlobals>(kPeer)[0];
    CHECK(cAll.Values.size() == 2);
    CHECK(ValueOf(cAll, kFriendsCountRift) == 3.f);
    CHECK(session.SentTo<NotifyGlobals>(kOtherPeer).empty());
}

namespace
{
const GameId kCorneredRat(0, 0x36191);
const GameId kAlduinsWall(0, 0x36192);

QuestState Quest(const GameId& acId, uint16_t aFlags, uint16_t aStage, std::vector<uint16_t> aDone = {})
{
    QuestState quest;
    quest.Id = acId;
    quest.Flags = aFlags;
    quest.CurrentStage = aStage;
    quest.DoneStages = std::move(aDone);
    return quest;
}
} // namespace

TEST_CASE("World state: the first look sends every quest, as a load", "[HostAuthority][WorldState]")
{
    Session session;
    session.Quests = {Quest(kCorneredRat, 0x1, 10, {5, 10}), Quest(kAlduinsWall, 0x0, 0)};
    session.Frame(1000);
    REQUIRE(session.SentTo<NotifyQuests>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyQuests>(kPeer)[0]->Join);
    CHECK(session.SentTo<NotifyQuests>(kPeer)[0]->Quests.size() == 2);
    CHECK(session.SentTo<NotifyQuests>(kHost).empty());
}

TEST_CASE("World state: a quest the host's game changes reaches the others, whole, as play", "[HostAuthority][WorldState]")
{
    Session session;
    session.Quests = {Quest(kCorneredRat, 0x1, 10, {5, 10}), Quest(kAlduinsWall, 0x0, 0)};
    session.Frame(1000);
    session.Outbox.clear();

    // Its stage, an objective, an alias fill: each a change of the whole quest.
    session.Quests[0].CurrentStage = 20;
    session.Quests[0].DoneStages.push_back(20);
    session.Quests[0].Objectives.push_back({20, 1});
    session.Quests[0].Aliases.push_back({3, GameId(0, 0x1A677), false});
    session.QuestChanges = {session.Quests[0]};
    session.Frame(1001);
    REQUIRE(session.SentTo<NotifyQuests>(kOtherPeer).size() == 1);
    const auto& cChanged = *session.SentTo<NotifyQuests>(kOtherPeer)[0];
    CHECK_FALSE(cChanged.Join);
    REQUIRE(cChanged.Quests.size() == 1);
    CHECK(cChanged.Quests[0] == session.Quests[0]);
    CHECK(session.SentTo<NotifyQuests>(kHost).empty());

    // Nothing changed: nothing sent.
    session.Outbox.clear();
    session.Frame(2000);
    CHECK(session.SentTo<NotifyQuests>(kPeer).empty());
}

TEST_CASE("World state: quest writes go out next frame without re-reading quiet quests", "[HostAuthority][WorldState]")
{
    Session session;
    session.Quests = {Quest(kAlduinsWall, 0x0, 0)};
    session.Frame(1000);
    session.Outbox.clear();

    session.Quests[0].Flags = 0x1;
    session.QuestChanges = {session.Quests[0]};
    session.Frame(1001);
    REQUIRE(session.SentTo<NotifyQuests>(kPeer).size() == 1);
    CHECK_FALSE(session.SentTo<NotifyQuests>(kPeer)[0]->Join);
    session.Outbox.clear();
    session.Frame(100000);
    CHECK(session.SentTo<NotifyQuests>(kPeer).empty());
    CHECK(session.QuestReads == 1);

    session.QuestChanges = {session.Quests[0]};
    session.Frame(100001);
    CHECK(session.SentTo<NotifyQuests>(kPeer).empty());
}

TEST_CASE("World state: an empty initial quest list is still read only once", "[HostAuthority][WorldState]")
{
    Session session;
    session.Frame(1000);
    session.Quests = {Quest(kAlduinsWall, 0x1, 0)};
    session.QuestChanges = session.Quests;
    session.Frame(1001);
    REQUIRE(session.SentTo<NotifyQuests>(kPeer).size() == 1);
    CHECK_FALSE(session.SentTo<NotifyQuests>(kPeer)[0]->Join);
    CHECK(session.QuestReads == 1);
}

TEST_CASE("World state: a joining player gets every quest, as a load", "[HostAuthority][WorldState]")
{
    Session session;
    session.Quests = {Quest(kCorneredRat, 0x3, 200, {5, 10, 200}), Quest(kAlduinsWall, 0x1, 10, {10})};
    session.Frame(1000);
    session.Outbox.clear();

    AnnounceCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    announce.SessionId = session.CharacterId(kPeer);
    session.Receive(kPeer, announce);
    REQUIRE(session.SentTo<NotifyQuests>(kPeer).size() == 1);
    const auto& cAll = *session.SentTo<NotifyQuests>(kPeer)[0];
    CHECK(cAll.Join);
    REQUIRE(cAll.Quests.size() == 2);
    CHECK(cAll.Quests[0] == session.Quests[0]);
    CHECK(cAll.Quests[1] == session.Quests[1]);
    CHECK(session.SentTo<NotifyQuests>(kOtherPeer).empty());
}

TEST_CASE("World state: the host's world objects' changes reach the others in order, once", "[HostAuthority][WorldState]")
{
    Session session;
    // The sword in the stone: the box goes, its manager opens the secret door, an animation plays; a chest
    // holds two sweetrolls.
    ObjectChange items{ObjectChange::Kind::kItems, GameId(0, 0xA05B5)};
    items.Item = GameId(0, 0x64B3D);
    items.Value = 2;
    session.ObjectChanges = {{ObjectChange::Kind::kEnabled, GameId(0, 0xA05C0)},
                             {ObjectChange::Kind::kActivate, GameId(0, 0xC149D), GameId(0, 0xA05C4), 3, {}, {}},
                             {ObjectChange::Kind::kAnimation, GameId(0, 0xA05C0), {}, 0, {}, "Open"},
                             items};
    session.Frame(1000);
    for (const auto cTo : {kPeer, kOtherPeer})
    {
        const auto cSent = session.SentTo<NotifyObjectChanges>(cTo);
        REQUIRE(cSent.size() == 1);
        REQUIRE(cSent[0]->Changes.size() == 4);
        CHECK(cSent[0]->Changes[0].Type == ObjectChange::Kind::kEnabled);
        CHECK(cSent[0]->Changes[0].Value == 0);
        CHECK(cSent[0]->Changes[1].Type == ObjectChange::Kind::kActivate);
        CHECK(cSent[0]->Changes[1].Activator == GameId(0, 0xA05C4));
        CHECK(cSent[0]->Changes[2].Event == "Open");
        CHECK(cSent[0]->Changes[3] == items);
    }
    CHECK(session.SentTo<NotifyObjectChanges>(kHost).empty());

    // Taken once: nothing new, nothing sent.
    session.Outbox.clear();
    session.Frame(1016);
    CHECK(session.SentTo<NotifyObjectChanges>(kPeer).empty());
}

namespace
{
const GameId kInn(0, 0x133C6);
const GameId kInnDoor(0, 0x1A6F6);

CellStateRequest InnLoaded()
{
    CellStateRequest request;
    request.Cell = kInn;
    request.Changed = {{GameId(0, 0x1A6F7), 0x1000}};
    return request;
}

ObjectChange InnDoorLocked()
{
    ObjectChange lock{ObjectChange::Kind::kLock, kInnDoor};
    lock.Value = 0x100 | 50;
    return lock;
}
} // namespace

TEST_CASE("World state: a guest's loaded cell gets the host's state there, that guest alone", "[HostAuthority][WorldState]")
{
    Session session;
    session.LoadedCells[kInn.BaseId] = {InnDoorLocked()};
    session.Receive(kPeer, InnLoaded());

    const auto cSent = session.SentTo<NotifyObjectChanges>(kPeer);
    REQUIRE(cSent.size() == 1);
    CHECK(cSent[0]->Cell == kInn);
    REQUIRE(cSent[0]->Changes.size() == 1);
    CHECK(cSent[0]->Changes[0] == InnDoorLocked());
    // The guest's own changes there are read too.
    CHECK(session.GuestChanged == InnLoaded().Changed);
    CHECK(session.SentTo<NotifyObjectChanges>(kOtherPeer).empty());
    CHECK(session.SentTo<NotifyObjectChanges>(kHost).empty());
}

TEST_CASE("World state: a cell with nothing changed is still answered", "[HostAuthority][WorldState]")
{
    Session session;
    session.LoadedCells[kInn.BaseId] = {};
    session.Receive(kPeer, InnLoaded());

    const auto cSent = session.SentTo<NotifyObjectChanges>(kPeer);
    REQUIRE(cSent.size() == 1);
    CHECK(cSent[0]->Cell == kInn);
    CHECK(cSent[0]->Changes.empty());
}

TEST_CASE("World state: a guest's cell the host's game has not loaded is answered once it is", "[HostAuthority][WorldState]")
{
    Session session;
    session.Receive(kPeer, InnLoaded());
    session.Frame(1000);
    CHECK(session.SentTo<NotifyObjectChanges>(kPeer).empty());

    session.LoadedCells[kInn.BaseId] = {InnDoorLocked()};
    session.Frame(1016);
    REQUIRE(session.SentTo<NotifyObjectChanges>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyObjectChanges>(kPeer)[0]->Cell == kInn);

    // Answered once.
    session.Outbox.clear();
    session.Frame(1032);
    CHECK(session.SentTo<NotifyObjectChanges>(kPeer).empty());
}

TEST_CASE("World state: a guest that leaves is not answered for the cells it waited for", "[HostAuthority][WorldState]")
{
    Session session;
    session.Receive(kPeer, InnLoaded());
    session.Leave(kPeer);
    session.LoadedCells[kInn.BaseId] = {InnDoorLocked()};
    session.Outbox.clear();
    session.Frame(1000);
    CHECK(session.SentTo<NotifyObjectChanges>(kPeer).empty());
}

TEST_CASE("World state: the host's own cells are not asked for", "[HostAuthority][WorldState]")
{
    Session session;
    session.LoadedCells[kInn.BaseId] = {InnDoorLocked()};
    session.Receive(kHost, InnLoaded());
    CHECK(session.SentTo<NotifyObjectChanges>(kHost).empty());
}

TEST_CASE("World state: a large cell's state goes in several messages", "[HostAuthority][WorldState]")
{
    Session session;
    std::vector<ObjectChange> state(1500, InnDoorLocked());
    session.LoadedCells[kInn.BaseId] = state;
    session.Receive(kPeer, InnLoaded());

    const auto cSent = session.SentTo<NotifyObjectChanges>(kPeer);
    REQUIRE(cSent.size() == 2);
    CHECK(cSent[0]->Changes.size() + cSent[1]->Changes.size() == 1500);
    CHECK(cSent[1]->Cell == kInn);
}
