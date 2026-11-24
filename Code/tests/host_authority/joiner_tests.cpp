// A player joining gets what is held now, from one list of stores (docs/design/one-simulator.md, piece 9).
#include "session.h"

#include <Messages/CrimeMessages.h>
#include <Messages/NotifyTimeScale.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/WeatherRegionRequest.h>
#include <Messages/SharedDialogue.h>
#include <Messages/TimeScaleRequest.h>
#include <Messages/WorldState.h>

namespace
{
// aPlayer's character arrives, as its game announces it; returns its session id.
uint32_t Arrive(Session& aSession, ConnectionId_t aPlayer)
{
    AnnounceCharacterRequest announce;
    announce.ReferenceId = GameId(0, 0x14);
    announce.SessionId = aSession.CharacterId(aPlayer);
    aSession.Receive(aPlayer, announce);
    return announce.SessionId;
}

RoutedCallRequest ControlsCall(CallControls aControls)
{
    RoutedCallRequest call;
    call.To = CallTo::kOthers;
    call.Call.Class = "Game";
    call.Call.Function = aControls == CallControls::kTakes ? "DisablePlayerControls" : "EnablePlayerControls";
    call.Call.Arguments = {true, true, false, false, false, true, true, false, int32_t{0}};
    call.Controls = aControls;
    return call;
}

RequestSetWaypoint Waypoint()
{
    RequestSetWaypoint set;
    set.Position.x = 1000.f;
    set.Position.y = 2000.f;
    set.Position.z = 30.f;
    set.WorldSpaceFormID = GameId(0, 0x3C);
    return set;
}

// The host's NPC talking with the party (its menu opened), at step 2 after the other guest's pick.
uint32_t Conversation(Session& aSession)
{
    DialogueOpenedRequest opened;
    opened.NpcId = aSession.Character(kHost);
    opened.Greeting = GameId(0, 0x5A1);
    aSession.Receive(kHost, opened);
    const uint32_t cId = aSession.SentTo<NotifyDialogueOpen>(kHost).at(0)->Conversation;
    DialogueOptionsRequest list;
    list.Conversation = cId;
    list.Step = 1;
    list.Options.emplace_back().Topic = GameId(0, 0xA);
    list.Options.back().Info = GameId(0, 0xA1);
    aSession.Receive(kHost, list);
    DialogueJoinRequest join;
    join.Conversation = cId;
    aSession.Receive(kOtherPeer, join);
    DialoguePickRequest pick;
    pick.Conversation = cId;
    pick.Step = 1;
    pick.Topic = GameId(0, 0xA);
    pick.Info = GameId(0, 0xA1);
    aSession.Receive(kOtherPeer, pick);
    REQUIRE(!aSession.SentTo<NotifyDialoguePicked>(kHost).empty());
    return cId;
}
} // namespace

TEST_CASE("A joiner gets each game's cutscene still holding the controls, as that game's call", "[HostAuthority][Join]")
{
    Session session;
    Arrive(session, kPeer);
    session.Receive(kHost, ControlsCall(CallControls::kTakes));
    session.Receive(kPeer, ControlsCall(CallControls::kTakes));
    session.Outbox.clear();

    Arrive(session, kOtherPeer);
    const auto cHeld = session.SentTo<NotifyRoutedCall>(kOtherPeer);
    REQUIRE(cHeld.size() == 2);
    CHECK(cHeld[0]->Source != cHeld[1]->Source);
    for (const auto* cpCall : cHeld)
    {
        CHECK(cpCall->Call.Function == "DisablePlayerControls");
        CHECK((cpCall->Source == session.PlayerId(kHost) || cpCall->Source == session.PlayerId(kPeer)));
    }

    SECTION("a cutscene given back is not sent")
    {
        session.Receive(kHost, ControlsCall(CallControls::kGivesBack));
        session.Outbox.clear();
        Arrive(session, kOtherPeer);
        const auto cAfter = session.SentTo<NotifyRoutedCall>(kOtherPeer);
        REQUIRE(cAfter.size() == 1);
        CHECK(cAfter[0]->Source == session.PlayerId(kPeer));
    }
    SECTION("a game that left holds nothing")
    {
        session.Leave(kPeer);
        session.Outbox.clear();
        Arrive(session, kOtherPeer);
        const auto cAfter = session.SentTo<NotifyRoutedCall>(kOtherPeer);
        REQUIRE(cAfter.size() == 1);
        CHECK(cAfter[0]->Source == session.PlayerId(kHost));
    }
    SECTION("its own cutscene is not sent back to it")
    {
        session.Receive(kOtherPeer, ControlsCall(CallControls::kTakes));
        session.Outbox.clear();
        Arrive(session, kOtherPeer);
        CHECK(session.SentTo<NotifyRoutedCall>(kOtherPeer).size() == 2);
    }
}

TEST_CASE("A joiner gets every controls call a cutscene made since it last gave back", "[HostAuthority][Join]")
{
    Session session;
    session.Receive(kHost, ControlsCall(CallControls::kTakes)); // movement and fighting
    RoutedCallRequest movement = ControlsCall(CallControls::kTakes);
    movement.Call.Arguments[1] = false; // then movement alone: fighting stays taken
    session.Receive(kHost, movement);
    session.Outbox.clear();

    Arrive(session, kPeer);
    const auto cHeld = session.SentTo<NotifyRoutedCall>(kPeer);
    REQUIRE(cHeld.size() == 2);
    CHECK(cHeld[0]->Call.Arguments == ControlsCall(CallControls::kTakes).Call.Arguments);
    CHECK(cHeld[1]->Call.Arguments == movement.Call.Arguments);
}

TEST_CASE("A party call that is not on the controls is not kept for a joiner", "[HostAuthority][Join]")
{
    Session session;
    RoutedCallRequest shake;
    shake.To = CallTo::kOthers;
    shake.Call.Class = "Game";
    shake.Call.Function = "ShakeCamera";
    session.Receive(kHost, shake);
    session.Outbox.clear();

    Arrive(session, kPeer);
    CHECK(session.SentTo<NotifyRoutedCall>(kPeer).empty());
}

TEST_CASE("A joiner gets the party's map marker, none once removed", "[HostAuthority][Join]")
{
    Session session;
    session.Receive(kHost, Waypoint());
    session.Outbox.clear();

    Arrive(session, kPeer);
    const auto cMarker = session.SentTo<NotifySetWaypoint>(kPeer);
    REQUIRE(cMarker.size() == 1);
    CHECK(cMarker[0]->Position == Waypoint().Position);
    CHECK(cMarker[0]->WorldSpaceFormID == GameId(0, 0x3C));

    session.Receive(kOtherPeer, RequestRemoveWaypoint{});
    session.Outbox.clear();
    Arrive(session, kPeer);
    CHECK(session.SentTo<NotifySetWaypoint>(kPeer).empty());
}

TEST_CASE("A joiner gets the conversation under way at its step, and joins it by talking", "[HostAuthority][Join]")
{
    Session session;
    const uint32_t cId = Conversation(session);
    session.Outbox.clear();

    Arrive(session, kPeer);
    const auto cOpen = session.SentTo<NotifyDialogueOpen>(kPeer);
    REQUIRE(cOpen.size() == 1);
    CHECK(cOpen[0]->Conversation == cId);
    CHECK(cOpen[0]->Step == 2);
    CHECK_FALSE(cOpen[0]->Everyone);
    CHECK(cOpen[0]->Greeting == GameId(0, 0x5A1));

    // No list until it joins: the host's game shows step 2's, and the joiner's menu opens on it.
    CHECK(session.SentTo<NotifyDialogueList>(kPeer).empty());
    DialogueOptionsRequest list;
    list.Conversation = cId;
    list.Step = 2;
    list.Options.emplace_back().Topic = GameId(0, 0xB);
    session.Receive(kHost, list);
    DialogueJoinRequest join;
    join.Conversation = cId;
    session.Receive(kPeer, join);
    const auto cLists = session.SentTo<NotifyDialogueList>(kPeer);
    REQUIRE(cLists.size() == 1);
    CHECK(cLists[0]->Step == 2);

    SECTION("none once it ended")
    {
        DialogueCloseRequest close;
        close.Conversation = cId;
        close.Ended = true;
        session.Receive(kHost, close);
        session.Outbox.clear();
        Arrive(session, kOtherPeer);
        CHECK(session.SentTo<NotifyDialogueOpen>(kOtherPeer).empty());
    }
}

TEST_CASE("Every store answers a joiner", "[HostAuthority][Join]")
{
    Session session;
    // Something held in every store.
    session.Weather = GameId(0, 0x81A);
    session.Quests = {QuestState{.Id = GameId(0, 0x36191), .CurrentStage = 10}};
    session.Frame(1000);
    TimeScaleRequest slow;
    slow.Multiplier = 0.25f;
    session.Receive(kHost, slow);
    session.Character(kOtherPeer);
    session.Receive(kHost, ControlsCall(CallControls::kTakes));
    session.Receive(kHost, Waypoint());
    Conversation(session);
    session.Receive(kPeer, WeatherRegionRequest{}); // its first report: no region
    session.Outbox.clear();

    Arrive(session, kPeer);
    // Each store's answer, by its name on the list: a store added to the list needs its row here, and a store holding
    // session state that is not on the list sends nothing (its row fails).
    const std::map<std::string_view, std::function<bool()>> cAnswers{
        {"crime", [&] { return !session.SentTo<NotifyCrimeState>(kPeer).empty(); }},
        {"clock", [&] { return !session.SentTo<NotifyTimeSettings>(kPeer).empty(); }},
        {"weather", [&] { return !session.SentTo<NotifyWeatherChange>(kPeer).empty(); }},
        {"world state", [&] { return !session.SentTo<NotifyQuests>(kPeer).empty(); }},
        {"time scale", [&] { return !session.SentTo<NotifyTimeScale>(kPeer).empty(); }},
        {"characters", [&] { return !session.SentTo<NotifyCharacterSpawn>(kPeer).empty(); }},
        {"purse", [&] { return !session.SentTo<NotifyPartyGold>(kPeer).empty(); }},
        {"party items", [&] { return !session.SentTo<NotifyPartyItems>(kPeer).empty(); }},
        {"cutscene controls", [&] { return !session.SentTo<NotifyRoutedCall>(kPeer).empty(); }},
        {"waypoint", [&] { return !session.SentTo<NotifySetWaypoint>(kPeer).empty(); }},
        {"conversation", [&] { return !session.SentTo<NotifyDialogueOpen>(kPeer).empty(); }},
    };
    std::set<std::string_view> listed;
    for (const auto& cStore : session.Authority().JoinerStores())
    {
        INFO("store: " << cStore.Name);
        listed.insert(cStore.Name);
        const auto cAnswer = cAnswers.find(cStore.Name);
        REQUIRE(cAnswer != cAnswers.end());
        CHECK(cAnswer->second());
    }
    for (const auto& [cName, cAnswer] : cAnswers)
    {
        INFO("store not on the list: " << cName);
        CHECK(listed.contains(cName));
    }
}
