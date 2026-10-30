// HostDialogue: one conversation for the party, run by the host's game (docs/design/shared-dialogue.md).
#include "session.h"

#include <Messages/SharedDialogue.h>

namespace
{
// The host's NPC and a conversation open with it (the host's menu opened); what that sent is cleared.
struct Talking
{
    Session session;
    uint32_t Npc = session.Character(kHost);
    uint32_t Id = Open();

    uint32_t Open()
    {
        DialogueOpenedRequest opened;
        opened.NpcId = Npc;
        session.Receive(kHost, opened);
        const auto cOpen = session.SentTo<NotifyDialogueOpen>(kPeer);
        REQUIRE(cOpen.size() == 1);
        const uint32_t cId = cOpen[0]->Conversation;
        session.Outbox.clear(); // frees what cOpen points at
        return cId;
    }

    // The host's game's list in these tests: four topics, each with one line (topic T says line T1).
    static DialogueOptions Options()
    {
        DialogueOptions options;
        for (const auto& [cTopic, cpText] : {std::pair{0xAu, "Tell me about A."}, std::pair{0xBu, "(Persuade) B?"},
                                             std::pair{0xCu, "Here. Have a gold piece."}, std::pair{0xEu, "Goodbye."}})
        {
            DialogueOption& option = options.emplace_back();
            option.Topic = GameId(0, cTopic);
            option.Info = GameId(0, cTopic * 0x10 + 1);
            option.Text = cpText;
        }
        return options;
    }

    // That game showed the step's list (aChoices: a line's choices).
    void List(ConnectionId_t aFrom, uint32_t aStep, bool aChoices = false, DialogueOptions aOptions = Options())
    {
        DialogueOptionsRequest list;
        list.Conversation = Id;
        list.Step = aStep;
        list.Choices = aChoices;
        list.Options = std::move(aOptions);
        session.Receive(aFrom, list);
    }

    // A pick from that game's menu, which joined it: topic T with line T1 unless told otherwise.
    void Pick(ConnectionId_t aFrom, uint32_t aStep, uint32_t aTopic, std::optional<GameId> aInfo = {}, const char* acpText = "")
    {
        DialogueJoinRequest join;
        join.Conversation = Id;
        session.Receive(aFrom, join);
        DialoguePickRequest pick;
        pick.Conversation = Id;
        pick.Step = aStep;
        pick.Topic = GameId(0, aTopic);
        pick.Info = aInfo.value_or(GameId(0, aTopic * 0x10 + 1));
        pick.Text = acpText;
        session.Receive(aFrom, pick);
    }

    const NotifyDialogueList& LastList(ConnectionId_t aTo)
    {
        const auto cLists = session.SentTo<NotifyDialogueList>(aTo);
        REQUIRE(!cLists.empty());
        return *cLists.back();
    }
};

DialogueStartRequest Start(uint32_t aNpc)
{
    DialogueStartRequest start;
    start.NpcId = aNpc;
    return start;
}

DialogueCloseRequest Close(uint32_t aConversation, bool aEnded = false)
{
    DialogueCloseRequest close;
    close.Conversation = aConversation;
    close.Ended = aEnded;
    return close;
}

DialogueJoinRequest Join(uint32_t aConversation)
{
    DialogueJoinRequest join;
    join.Conversation = aConversation;
    return join;
}
} // namespace

TEST_CASE("Dialogue: a peer talking to a copy starts the host's conversation", "[HostAuthority][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);

    session.Receive(kPeer, Start(cNpc));
    const auto cStarts = session.SentTo<NotifyDialogueStart>(kHost);
    REQUIRE(cStarts.size() == 1);
    CHECK(cStarts[0]->NpcId == cNpc);
    // Nobody else opens anything until the host's menu is open.
    CHECK(session.SentTo<NotifyDialogueOpen>(kPeer).empty());
    CHECK(session.SentTo<NotifyDialogueOpen>(kOtherPeer).empty());
}

TEST_CASE("Dialogue: the peer who asked is in the conversation from the start", "[HostAuthority][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);
    session.Receive(kPeer, Start(cNpc));
    DialogueOpenedRequest opened;
    opened.NpcId = cNpc;
    session.Receive(kHost, opened);
    const auto cOpen = session.SentTo<NotifyDialogueOpen>(kPeer);
    REQUIRE(cOpen.size() == 1);
    const uint32_t cId = cOpen[0]->Conversation;

    // Before its menu is open: another player's exit ends nothing, its own (its menu did not open) ends it.
    session.Receive(kOtherPeer, Close(cId));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
    session.Receive(kPeer, Close(cId));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).size() == 1);
}

TEST_CASE("Dialogue: the host talking itself puts nobody in it", "[HostAuthority][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);
    // A start the host's game never ran (busy) leaves no asker for the next conversation.
    session.Receive(kPeer, Start(session.Character(kHost)));
    DialogueOpenedRequest opened;
    opened.NpcId = cNpc;
    session.Receive(kHost, opened);
    const uint32_t cId = session.SentTo<NotifyDialogueOpen>(kPeer).at(0)->Conversation;

    session.Receive(kHost, Join(cId));
    session.Receive(kPeer, Close(cId));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
    session.Receive(kHost, Close(cId));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).size() == 1);
}

TEST_CASE("Dialogue: only the host's NPCs talk", "[HostAuthority][Dialogue]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    session.Receive(kOtherPeer, Start(cPeerPlayer));
    session.Receive(kHost, Start(cPeerPlayer));
    session.Receive(kOtherPeer, Start(0x7777));
    CHECK(session.SentTo<NotifyDialogueStart>(kHost).empty());
}

TEST_CASE("Dialogue: the host's menu opening opens it for everyone", "[HostAuthority][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);

    DialogueOpenedRequest opened;
    opened.NpcId = cNpc;
    session.Receive(kHost, opened);
    uint32_t id = 0;
    // The host's game too: it learns the conversation's id.
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto cOpen = session.SentTo<NotifyDialogueOpen>(cTo);
        REQUIRE(cOpen.size() == 1);
        CHECK(cOpen[0]->NpcId == cNpc);
        CHECK(cOpen[0]->Conversation != 0);
        if (id)
            CHECK(cOpen[0]->Conversation == id);
        id = cOpen[0]->Conversation;
    }
}

TEST_CASE("Dialogue: a peer cannot say the host's menu opened", "[HostAuthority][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);

    DialogueOpenedRequest opened;
    opened.NpcId = cNpc;
    session.Receive(kPeer, opened);
    CHECK(session.SentTo<NotifyDialogueOpen>(kOtherPeer).empty());
}

TEST_CASE("Dialogue: one conversation at a time", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    const uint32_t cOtherNpc = session.Character(kHost);

    session.Receive(kOtherPeer, Start(cOtherNpc));
    CHECK(session.SentTo<NotifyDialogueStart>(kHost).empty());
    // The refused player hears why, as a session line.
    const auto cLines = session.SentTo<NotifyChatMessageBroadcast>(kOtherPeer);
    REQUIRE(cLines.size() == 1);
    CHECK(cLines[0]->MessageType == kSystemMessage);
    CHECK(session.SentTo<NotifyChatMessageBroadcast>(kPeer).empty());
}

TEST_CASE("Dialogue: talking to the NPC of the conversation joins it, no new start", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;

    session.Receive(kOtherPeer, Start(talking.Npc));
    CHECK(session.SentTo<NotifyDialogueStart>(kHost).empty());
    CHECK(session.SentTo<NotifyChatMessageBroadcast>(kOtherPeer).empty());
}

TEST_CASE("Dialogue: a player leaving leaves it on for the others; the last one out ends it", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    for (const auto cFrom : {kHost, kPeer, kOtherPeer})
        session.Receive(cFrom, Join(talking.Id));

    session.Receive(kHost, Close(talking.Id));
    session.Receive(kPeer, Close(talking.Id));
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
        CHECK(session.SentTo<NotifyDialogueClosed>(cTo).empty());

    // The last one out: every game hears it, the host's too (its conversation still runs without the menu).
    session.Receive(kOtherPeer, Close(talking.Id));
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto cClosed = session.SentTo<NotifyDialogueClosed>(cTo);
        REQUIRE(cClosed.size() == 1);
        CHECK(cClosed[0]->Conversation == talking.Id);
    }
    session.Outbox.clear();
    session.Receive(kOtherPeer, Start(talking.Npc));
    CHECK(session.SentTo<NotifyDialogueStart>(kHost).size() == 1);
}

TEST_CASE("Dialogue: leaving twice, or without joining, ends nothing", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));
    session.Receive(kPeer, Join(talking.Id));

    session.Receive(kOtherPeer, Close(talking.Id));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
    session.Receive(kPeer, Close(talking.Id));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).size() == 1);
}

TEST_CASE("Dialogue: the host's conversation ending ends it for everyone", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));

    // Whatever ended it (a goodbye, the NPC walked off, combat), even before the host's game learnt the id.
    session.Receive(kHost, Close(0, true));
    for (const auto cTo : {kPeer, kOtherPeer})
        CHECK(session.SentTo<NotifyDialogueClosed>(cTo).size() == 1);
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
}

TEST_CASE("Dialogue: only the host's game ends it outright", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kHost, Join(talking.Id));

    session.Receive(kPeer, Close(talking.Id, true));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
}

TEST_CASE("Dialogue: a close for an old conversation is dropped", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kHost, Close(talking.Id, true));
    const uint32_t cOld = talking.Id;
    talking.Id = talking.Open();
    session.Receive(kPeer, Join(talking.Id));

    session.Receive(kPeer, Close(cOld));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
    CHECK(session.SentTo<NotifyDialogueClosed>(kOtherPeer).empty());
}

TEST_CASE("Dialogue: a player who disconnects leaves it", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));

    session.Leave(kPeer);
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).size() == 1);
}

TEST_CASE("Dialogue: the host talking to another NPC ends the first conversation", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    const uint32_t cFirst = talking.Id;

    talking.Npc = session.Character(kHost);
    session.Outbox.clear();
    DialogueOpenedRequest opened;
    opened.NpcId = talking.Npc;
    session.Receive(kHost, opened);
    const auto cClosed = session.SentTo<NotifyDialogueClosed>(kPeer);
    REQUIRE(cClosed.size() == 1);
    CHECK(cClosed[0]->Conversation == cFirst);
    const auto cOpen = session.SentTo<NotifyDialogueOpen>(kPeer);
    REQUIRE(cOpen.size() == 1);
    CHECK(cOpen[0]->NpcId == talking.Npc);
    CHECK(cOpen[0]->Conversation != cFirst);
}

TEST_CASE("Dialogue: the host's game's list reaches the games in the conversation", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));
    talking.List(kHost, 1);
    const auto& cList = talking.LastList(kPeer);
    CHECK(cList.Conversation == talking.Id);
    CHECK(cList.Step == 1);
    CHECK_FALSE(cList.Choices);
    CHECK(cList.Options == Talking::Options()); // in the host's menu order, its lines and texts
    // Not the host's own game (its engine built it), nor a game not in it.
    CHECK(session.SentTo<NotifyDialogueList>(kHost).empty());
    CHECK(session.SentTo<NotifyDialogueList>(kOtherPeer).empty());

    // A line's choices.
    session.Outbox.clear();
    talking.List(kHost, 1, true);
    CHECK(talking.LastList(kPeer).Choices);

    // A guest's game sends none.
    session.Outbox.clear();
    talking.List(kPeer, 1);
    CHECK(session.SentTo<NotifyDialogueList>(kPeer).empty());
}

TEST_CASE("Dialogue: a game joining gets the current list", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.List(kHost, 1, true);
    CHECK(session.SentTo<NotifyDialogueList>(kOtherPeer).empty());
    session.Receive(kOtherPeer, Join(talking.Id));
    CHECK(talking.LastList(kOtherPeer).Options == Talking::Options());
    CHECK(talking.LastList(kOtherPeer).Choices);
}

TEST_CASE("Dialogue: a list for another step or conversation is dropped", "[HostAuthority][Dialogue]")
{
    Talking talking;
    talking.session.Receive(kPeer, Join(talking.Id));

    talking.List(kHost, 2);
    CHECK(talking.session.SentTo<NotifyDialogueList>(kPeer).empty());
    talking.Id += 1;
    talking.List(kHost, 1);
    CHECK(talking.session.SentTo<NotifyDialogueList>(kPeer).empty());
}

TEST_CASE("Dialogue: the first pick of a step wins, and every game hears it", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.List(kHost, 1);
    session.Outbox.clear();

    talking.Pick(kPeer, 1, 0xA);
    talking.Pick(kOtherPeer, 1, 0xB);
    talking.Pick(kHost, 1, 0xB);
    // The host's game too: it says it (unless its own menu did).
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto cPicked = session.SentTo<NotifyDialoguePicked>(cTo);
        REQUIRE(cPicked.size() == 1);
        CHECK(cPicked[0]->Conversation == talking.Id);
        CHECK(cPicked[0]->Step == 2);
        CHECK(cPicked[0]->Topic == GameId(0, 0xA));
        CHECK(cPicked[0]->Info == GameId(0, 0xA1));
        CHECK(cPicked[0]->PlayerId == session.PlayerId(kPeer));
    }
}

TEST_CASE("Dialogue: only a player in the conversation picks", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.List(kHost, 1);
    session.Outbox.clear();

    // Far away, with no menu on it.
    DialoguePickRequest pick;
    pick.Conversation = talking.Id;
    pick.Step = 1;
    pick.Topic = GameId(0, 0xA);
    pick.Info = GameId(0, 0xA1);
    session.Receive(kOtherPeer, pick);
    CHECK(session.SentTo<NotifyDialoguePicked>(kHost).empty());

    session.Receive(kOtherPeer, Join(talking.Id));
    session.Receive(kOtherPeer, pick);
    CHECK(session.SentTo<NotifyDialoguePicked>(kHost).size() == 1);
}

TEST_CASE("Dialogue: a pick is a line of the party's list, the picker's own", "[HostAuthority][Dialogue]")
{
    // A topic nobody in it has (one only the guest's engine offered) is not said, and the presser's menu gets the
    // party's list again; neither is a line of a listed topic that is not the picker's (each line is its own option).
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));
    talking.List(kHost, 1);
    session.Outbox.clear();

    talking.Pick(kPeer, 1, 0xD);
    CHECK(session.SentTo<NotifyDialoguePicked>(kHost).empty());
    CHECK(session.SentTo<NotifyDialogueList>(kPeer).size() == 1);
    CHECK(talking.LastList(kPeer).Step == 1);

    talking.Pick(kPeer, 1, 0xB, GameId(0, 0xB2));
    CHECK(session.SentTo<NotifyDialoguePicked>(kOtherPeer).empty());

    talking.Pick(kPeer, 1, 0xB);
    const auto cPicked = session.SentTo<NotifyDialoguePicked>(kOtherPeer);
    REQUIRE(cPicked.size() == 1);
    CHECK(cPicked[0]->Info == GameId(0, 0xB1));
}

namespace
{
// The step's list from the host's game with the guests' own lists: the peer holds Lucan's claw (topic F, only its
// list) and lacks the Speech for the persuade line (B); the other peer's own list was not judged (the host's).
void PartyList(Talking& aTalking)
{
    auto& session = aTalking.session;
    DialogueOptions own = Talking::Options();
    own.erase(own.begin() + 1);
    DialogueOption& claw = own.emplace_back();
    claw.Topic = GameId(0, 0xF);
    claw.Info = GameId(0, 0xF1);
    claw.Text = "I have your claw.";
    DialogueOptionsRequest list;
    list.Conversation = aTalking.Id;
    list.Step = 1;
    list.Options = Talking::Options();
    list.Players = {{session.PlayerId(kPeer), own}, {session.PlayerId(kOtherPeer), Talking::Options()}};
    session.Receive(kHost, list);
}
} // namespace

TEST_CASE("Dialogue: every menu shows the party's list, each option tagged with who has it", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));
    session.Receive(kOtherPeer, Join(talking.Id));
    PartyList(talking);

    // The host's order, then the peer's own topic; everyone's untagged.
    const auto& cList = talking.LastList(kPeer);
    CHECK(talking.LastList(kOtherPeer).Options == cList.Options);
    std::vector<uint32_t> topics;
    for (const auto& cOption : cList.Options)
        topics.push_back(cOption.Topic.BaseId);
    CHECK(topics == std::vector<uint32_t>{0xA, 0xB, 0xC, 0xE, 0xF});
    CHECK(cList.Options[0].Players.empty());
    CHECK(cList.Options[1].Players == std::vector<uint32_t>{session.PlayerId(kOtherPeer)});
    CHECK(cList.Options[4].Players == std::vector<uint32_t>{session.PlayerId(kPeer)});

    // The host's menu opening adds it: everyone's options stay untagged, the persuade line is the host's too.
    session.Receive(kHost, Join(talking.Id));
    const auto& cWithHost = talking.LastList(kHost);
    CHECK(cWithHost.Options[1].Players == std::vector<uint32_t>{session.PlayerId(kOtherPeer), session.PlayerId(kHost)});
    CHECK(cWithHost.Options[0].Players.empty());
}

TEST_CASE("Dialogue: a topic with a line per player lists each line on every menu, named for who has it", "[HostAuthority][Dialogue]")
{
    // Lucan's claw (2026-10-30, the user): the holder's "I have your claw." and the others' line are two options of
    // one topic, on every menu alike, the holder's right after the others'.
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));
    session.Receive(kHost, Join(talking.Id));
    DialogueOptions own = Talking::Options();
    own[0].Info = GameId(0, 0xA9);
    own[0].Text = "I have your claw.";
    DialogueOptionsRequest list;
    list.Conversation = talking.Id;
    list.Step = 1;
    list.Options = Talking::Options();
    list.Players = {{session.PlayerId(kPeer), own}};
    session.Receive(kHost, list);

    const auto& cList = talking.LastList(kPeer);
    CHECK(talking.LastList(kHost).Options == cList.Options);
    REQUIRE(cList.Options.size() == 5);
    CHECK((cList.Options[0].Info == GameId(0, 0xA1) && cList.Options[0].Players == std::vector<uint32_t>{session.PlayerId(kHost)}));
    CHECK((cList.Options[1].Info == GameId(0, 0xA9) && cList.Options[1].Players == std::vector<uint32_t>{session.PlayerId(kPeer)}));
    CHECK(cList.Options[2].Players.empty());

    session.Outbox.clear();
    talking.Pick(kHost, 1, 0xA, GameId(0, 0xA9));
    CHECK(session.SentTo<NotifyDialoguePicked>(kPeer).empty());
    talking.Pick(kPeer, 1, 0xA, GameId(0, 0xA9));
    const auto cPicked = session.SentTo<NotifyDialoguePicked>(kHost);
    REQUIRE(cPicked.size() == 1);
    CHECK((cPicked[0]->Info == GameId(0, 0xA9) && cPicked[0]->PlayerId == session.PlayerId(kPeer)));
}

TEST_CASE("Dialogue: a topic's random lines with one text are one option, said with each picker's own line", "[HostAuthority][Dialogue]")
{
    // A topic with random lines: judged for each player, the same prompt can come with another line. One option, for
    // everyone; a pick says the picker's own line.
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));
    session.Receive(kHost, Join(talking.Id));
    DialogueOptions own = Talking::Options();
    own[0].Info = GameId(0, 0xA2); // "Tell me about A." again
    DialogueOptionsRequest list;
    list.Conversation = talking.Id;
    list.Step = 1;
    list.Options = Talking::Options();
    list.Players = {{session.PlayerId(kPeer), own}};
    session.Receive(kHost, list);

    const auto& cList = talking.LastList(kPeer);
    REQUIRE(cList.Options.size() == 4);
    CHECK((cList.Options[0].Info == GameId(0, 0xA1) && cList.Options[0].Players.empty()));

    session.Outbox.clear();
    talking.Pick(kPeer, 1, 0xA);
    const auto cPicked = session.SentTo<NotifyDialoguePicked>(kHost);
    REQUIRE(cPicked.size() == 1);
    CHECK(cPicked[0]->Info == GameId(0, 0xA2));
}

TEST_CASE("Dialogue: a player says only its own options; another's is refused, and only it is told why", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));
    session.Receive(kOtherPeer, Join(talking.Id));
    session.Receive(kHost, Join(talking.Id));
    PartyList(talking);
    session.Outbox.clear();

    SECTION("its own: said with its line, the chat names it")
    {
        talking.Pick(kPeer, 1, 0xF);
        const auto cPicked = session.SentTo<NotifyDialoguePicked>(kHost);
        REQUIRE(cPicked.size() == 1);
        CHECK((cPicked[0]->PlayerId == session.PlayerId(kPeer) && cPicked[0]->Info == GameId(0, 0xF1)));
        const auto cLines = session.SentTo<NotifyChatMessageBroadcast>(kOtherPeer);
        REQUIRE(cLines.size() == 1);
        CHECK((cLines[0]->PlayerName == "Lydia" && cLines[0]->ChatMessage == "I have your claw."));
    }
    SECTION("the claw, pressed by a player without it: not said, its menu gets the list back, its chat says why")
    {
        talking.Pick(kOtherPeer, 1, 0xF);
        CHECK(session.SentTo<NotifyDialoguePicked>(kHost).empty());
        CHECK(talking.LastList(kOtherPeer).Step == 1);
        const auto cLines = session.SentTo<NotifyChatMessageBroadcast>(kOtherPeer);
        REQUIRE(cLines.size() == 1);
        CHECK(cLines[0]->MessageType == kSystemMessage);
        CHECK(cLines[0]->ChatMessage == "I'm sorry Aela, you don't know how to say that.");
        CHECK(session.SentTo<NotifyChatMessageBroadcast>(kPeer).empty());
        CHECK(session.SentTo<NotifyChatMessageBroadcast>(kHost).empty());
    }
    SECTION("the persuade line, pressed by the player without the Speech: refused; then said by one who has it")
    {
        talking.Pick(kPeer, 1, 0xB);
        CHECK(session.SentTo<NotifyDialoguePicked>(kHost).empty());
        talking.Pick(kOtherPeer, 1, 0xB);
        const auto cPicked = session.SentTo<NotifyDialoguePicked>(kHost);
        REQUIRE(cPicked.size() == 1);
        CHECK(cPicked[0]->PlayerId == session.PlayerId(kOtherPeer));
    }
}

TEST_CASE("Dialogue: the highlight goes to the others in the conversation, for the current step", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));
    session.Receive(kHost, Join(talking.Id));
    talking.List(kHost, 1);
    session.Outbox.clear();

    DialogueHighlightRequest moved;
    moved.Conversation = talking.Id;
    moved.Step = 1;
    moved.Index = 3;
    session.Receive(kPeer, moved);
    const auto cAtHost = session.SentTo<NotifyDialogueHighlight>(kHost);
    REQUIRE(cAtHost.size() == 1);
    CHECK((cAtHost[0]->Index == 3 && cAtHost[0]->PlayerId == session.PlayerId(kPeer)));
    CHECK(session.SentTo<NotifyDialogueHighlight>(kPeer).empty());
    CHECK(session.SentTo<NotifyDialogueHighlight>(kOtherPeer).empty()); // not in it

    moved.Step = 2; // an old or future step
    session.Receive(kPeer, moved);
    session.Receive(kOtherPeer, moved);
    CHECK(session.SentTo<NotifyDialogueHighlight>(kHost).size() == 1);
}

TEST_CASE("Dialogue: a pick before the host's list is not said", "[HostAuthority][Dialogue]")
{
    // The guest's menu opens on its own engine's list before the host's arrives.
    Talking talking;
    auto& session = talking.session;
    talking.Pick(kPeer, 1, 0xA);
    CHECK(session.SentTo<NotifyDialoguePicked>(kHost).empty());
    CHECK(session.SentTo<NotifyDialogueList>(kPeer).empty()); // nothing to send yet; it comes with the host's

    talking.List(kHost, 1);
    CHECK(talking.LastList(kPeer).Step == 1);
    talking.Pick(kPeer, 1, 0xA);
    CHECK(session.SentTo<NotifyDialoguePicked>(kHost).size() == 1);
}

TEST_CASE("Dialogue: a pick shows in everyone's chat as who said what", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.List(kHost, 1);
    session.Outbox.clear();

    talking.Pick(kPeer, 1, 0xC, {}, "Here. Have a gold piece.");
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto cLines = session.SentTo<NotifyChatMessageBroadcast>(cTo);
        REQUIRE(cLines.size() == 1);
        CHECK(cLines[0]->MessageType == kPlayerChat);
        CHECK(cLines[0]->PlayerName == "Lydia");
        CHECK(cLines[0]->ChatMessage == "Here. Have a gold piece.");
    }
}

TEST_CASE("Dialogue: after a pick, the next list waits for the host's game", "[HostAuthority][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));
    talking.List(kHost, 1);
    talking.Pick(kHost, 1, 0xA);
    session.Outbox.clear();

    // While the NPC answers: a late host list for the step picked is dropped, a pick for it too, and a pick for the
    // next step before the host's game showed its list.
    talking.List(kHost, 1);
    talking.Pick(kOtherPeer, 1, 0xA);
    talking.Pick(kOtherPeer, 2, 0xE);
    CHECK(session.SentTo<NotifyDialogueList>(kPeer).empty());
    CHECK(session.SentTo<NotifyDialoguePicked>(kHost).empty());

    DialogueOptions choices(1);
    choices[0].Topic = GameId(0, 0xF);
    choices[0].Info = GameId(0, 0xF1);
    talking.List(kHost, 2, true, choices);
    CHECK(talking.LastList(kPeer).Step == 2);
    CHECK(talking.LastList(kPeer).Options == choices);
    talking.Pick(kOtherPeer, 2, 0xE); // the last step's option, not this one's
    CHECK(session.SentTo<NotifyDialoguePicked>(kHost).empty());
    talking.Pick(kOtherPeer, 2, 0xF);
    CHECK(session.SentTo<NotifyDialoguePicked>(kHost).size() == 1);
}

TEST_CASE("Dialogue: a start that cannot happen is refused, with why", "[HostAuthority][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);

    // Not one of the host's NPCs: refused, no line.
    session.Receive(kOtherPeer, Start(cPeerPlayer));
    REQUIRE(session.SentTo<NotifyDialogueRefused>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyChatMessageBroadcast>(kOtherPeer).empty());

    // The host's game could not start it: the asker hears why, and the next start goes through.
    session.Outbox.clear();
    session.Receive(kPeer, Start(cNpc));
    DialogueStartFailedRequest failed;
    failed.NpcId = cNpc;
    session.Receive(kOtherPeer, failed); // not the host's game
    CHECK(session.SentTo<NotifyDialogueRefused>(kPeer).empty());
    session.Receive(kHost, failed);
    const auto cRefused = session.SentTo<NotifyDialogueRefused>(kPeer);
    REQUIRE(cRefused.size() == 1);
    CHECK(cRefused[0]->NpcId == cNpc);
    CHECK(session.SentTo<NotifyChatMessageBroadcast>(kPeer).size() == 1);
}

TEST_CASE("Dialogue: an NPC greeting the party puts every player in it", "[HostAuthority][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);
    DialogueOpenedRequest opened;
    opened.NpcId = cNpc;
    opened.Everyone = true;
    opened.Greeting = GameId(0, 0x2A37C);
    session.Receive(kHost, opened);
    const auto cOpen = session.SentTo<NotifyDialogueOpen>(kPeer);
    REQUIRE(cOpen.size() == 1);
    CHECK(cOpen[0]->Everyone);
    CHECK(cOpen[0]->Greeting == opened.Greeting);
    const uint32_t cId = cOpen[0]->Conversation;

    // Each guest is in it: the conversation ends once both have left.
    session.Receive(kPeer, Close(cId));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
    session.Receive(kOtherPeer, Close(cId));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).size() == 1);
}
