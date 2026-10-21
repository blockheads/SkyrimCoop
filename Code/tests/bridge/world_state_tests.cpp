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

TEST_CASE("World state: a changed global reaches the others, not the host", "[Bridge][WorldState]")
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
    session.Frame(1300);
    REQUIRE(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
    REQUIRE(session.SentTo<NotifyGlobals>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyGlobals>(kPeer)[0]->Values.size() == 1);
    CHECK(ValueOf(*session.SentTo<NotifyGlobals>(kPeer)[0], kFriendsCountRift) == 1.f);
    CHECK(session.SentTo<NotifyGlobals>(kHost).empty());
}

TEST_CASE("World state: the clock's globals are never sent", "[Bridge][WorldState]")
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
    session.Frame(1300);
    CHECK(session.SentTo<NotifyGlobals>(kPeer).empty());

    // Nor to a joining player.
    AssignCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    session.Receive(kPeer, announce);
    REQUIRE(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyGlobals>(kPeer)[0]->Values.size() == 1);
}

TEST_CASE("World state: an unchanged global is not sent again", "[Bridge][WorldState]")
{
    Session session;
    session.Globals = {{kFriendsCountRift, 0.f}, {kWEEnabled, std::numeric_limits<float>::quiet_NaN()}};
    session.Frame(1000);
    session.Outbox.clear();

    session.Frame(1300);
    session.Frame(1600);
    CHECK(session.Outbox.empty());
}

TEST_CASE("World state: globals are compared a few times a second", "[Bridge][WorldState]")
{
    Session session;
    session.Globals = {{kFriendsCountRift, 0.f}};
    session.Frame(1000);
    session.Outbox.clear();

    session.Globals[0].Value = 2.f;
    session.Frame(1100);
    CHECK(session.Outbox.empty());
    session.Frame(1250);
    CHECK(session.SentTo<NotifyGlobals>(kPeer).size() == 1);
}

TEST_CASE("World state: a joining player gets every global", "[Bridge][WorldState]")
{
    Session session;
    session.Globals = {{kFriendsCountRift, 3.f}, {kWEEnabled, 1.f}};
    session.Frame(1000);
    session.Outbox.clear();

    AssignCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
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

QuestState Quest(const GameId& acId, uint16_t aFlags, uint16_t aStage, Vector<uint16_t> aDone = {})
{
    QuestState quest;
    quest.Id = acId;
    quest.Flags = aFlags;
    quest.CurrentStage = aStage;
    quest.DoneStages = std::move(aDone);
    return quest;
}
} // namespace

TEST_CASE("World state: the first look sends every quest, as a load", "[Bridge][WorldState]")
{
    Session session;
    session.Quests = {Quest(kCorneredRat, 0x1, 10, {5, 10}), Quest(kAlduinsWall, 0x0, 0)};
    session.Frame(1000);
    REQUIRE(session.SentTo<NotifyQuests>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyQuests>(kPeer)[0]->Join);
    CHECK(session.SentTo<NotifyQuests>(kPeer)[0]->Quests.size() == 2);
    CHECK(session.SentTo<NotifyQuests>(kHost).empty());
}

TEST_CASE("World state: a quest the host's game changes reaches the others, whole, as play", "[Bridge][WorldState]")
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
    session.Frame(1500);
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

TEST_CASE("World state: quests are compared twice a second", "[Bridge][WorldState]")
{
    Session session;
    session.Quests = {Quest(kAlduinsWall, 0x0, 0)};
    session.Frame(1000);
    session.Outbox.clear();

    session.Quests[0].Flags = 0x1;
    session.Frame(1250);
    CHECK(session.SentTo<NotifyQuests>(kPeer).empty());
    session.Frame(1500);
    CHECK(session.SentTo<NotifyQuests>(kPeer).size() == 1);
}

TEST_CASE("World state: a joining player gets every quest, as a load", "[Bridge][WorldState]")
{
    Session session;
    session.Quests = {Quest(kCorneredRat, 0x3, 200, {5, 10, 200}), Quest(kAlduinsWall, 0x1, 10, {10})};
    session.Frame(1000);
    session.Outbox.clear();

    AssignCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    session.Receive(kPeer, announce);
    REQUIRE(session.SentTo<NotifyQuests>(kPeer).size() == 1);
    const auto& cAll = *session.SentTo<NotifyQuests>(kPeer)[0];
    CHECK(cAll.Join);
    REQUIRE(cAll.Quests.size() == 2);
    CHECK(cAll.Quests[0] == session.Quests[0]);
    CHECK(cAll.Quests[1] == session.Quests[1]);
    CHECK(session.SentTo<NotifyQuests>(kOtherPeer).empty());
}

TEST_CASE("World state: the host's world scripts' doors and animations reach the others in order, once", "[Bridge][WorldState]")
{
    Session session;
    // The sword in the stone: its manager opens the secret door and the box plays its animation.
    session.ScriptResults = {{ScriptResult::Kind::kActivate, GameId(0, 0xC149D), GameId(0, 0xA05C4), 3, {}, {}},
                             {ScriptResult::Kind::kAnimation, GameId(0, 0xA05C0), {}, 0, {}, "Open"}};
    session.Frame(1000);
    for (const auto cTo : {kPeer, kOtherPeer})
    {
        const auto cSent = session.SentTo<NotifyScriptResults>(cTo);
        REQUIRE(cSent.size() == 1);
        REQUIRE(cSent[0]->Results.size() == 2);
        CHECK(cSent[0]->Results[0].Type == ScriptResult::Kind::kActivate);
        CHECK(cSent[0]->Results[0].Activator == GameId(0, 0xA05C4));
        CHECK(cSent[0]->Results[1].Event == "Open");
    }
    CHECK(session.SentTo<NotifyScriptResults>(kHost).empty());

    // Taken once: nothing new, nothing sent.
    session.Outbox.clear();
    session.Frame(1016);
    CHECK(session.SentTo<NotifyScriptResults>(kPeer).empty());
}
