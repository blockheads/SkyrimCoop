// The map and chat: a player's waypoint and chat lines.
#include "session.h"

TEST_CASE("Waypoints: a player's reaches everyone else", "[HostAuthority][Chat]")
{
    Session session;

    RequestSetWaypoint set;
    set.Position = glm::vec3(100.f, 200.f, 300.f);
    set.WorldSpaceFormID = GameId(0, 0x3C);
    session.Receive(kPeer, set);
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifySetWaypoint>(cTo);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->Position == set.Position);
        CHECK(cNotifies[0]->WorldSpaceFormID == set.WorldSpaceFormID);
    }
    CHECK(session.SentTo<NotifySetWaypoint>(kPeer).empty());

    session.Receive(kPeer, RequestRemoveWaypoint{});
    CHECK(session.SentTo<NotifyRemoveWaypoint>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRemoveWaypoint>(kPeer).empty());
}

TEST_CASE("Chat: to everyone, the sender too, under its name and as typed", "[HostAuthority][Chat]")
{
    Session session;

    SendChatMessageRequest chat;
    chat.ChatMessage = "<b>hello</b> there";
    session.Receive(kPeer, chat);
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifyChatMessageBroadcast>(cTo);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->MessageType == kPlayerChat);
        CHECK(cNotifies[0]->PlayerName == "Lydia");
        CHECK(cNotifies[0]->ChatMessage == "<b>hello</b> there");
    }
}

TEST_CASE("Chat: player text cannot become a system line", "[HostAuthority][Chat]")
{
    Session session;

    SendChatMessageRequest chat;
    chat.ChatMessage = "The session is ending";
    session.Receive(kPeer, chat);
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto cLines = session.SentTo<NotifyChatMessageBroadcast>(cTo);
        REQUIRE(cLines.size() == 1);
        CHECK(cLines[0]->MessageType == kPlayerChat);
        CHECK(cLines[0]->PlayerName == "Lydia");
        CHECK(cLines[0]->ChatMessage == chat.ChatMessage);
    }
}

TEST_CASE("Chat: a line is cut to the chat's length, whole characters, without control characters", "[HostAuthority][Chat]")
{
    // The chat box shows markup as text: it stays.
    CHECK(ChatText("1 < 2 and <3 >_>") == "1 < 2 and <3 >_>");
    CHECK(ChatText("one\ntwo\tthree\x7f") == "onetwothree");

    const std::string cLong(kChatMaxLength + 10, 'a');
    CHECK(ChatText(cLong).size() == kChatMaxLength);
    // An é ("\xc3\xa9") across the limit is left out, not cut in half.
    const std::string cAcross = std::string(kChatMaxLength - 1, 'a') + "\xc3\xa9";
    CHECK(ChatText(cAcross) == std::string(kChatMaxLength - 1, 'a'));
    const std::string cFits = std::string(kChatMaxLength - 2, 'a') + "\xc3\xa9";
    CHECK(ChatText(cFits) == cFits);
}
