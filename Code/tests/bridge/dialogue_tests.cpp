// HostDialogue: one conversation for the party, run by the host's game (docs/design/shared-dialogue.md).
#include "session.h"

#include <Messages/SharedDialogue.h>

namespace
{
DialogueOption Option(uint32_t aTopic, uint32_t aInfo, const char* acpText)
{
    DialogueOption option;
    option.Topic = GameId(0, aTopic);
    option.Info = GameId(0, aInfo);
    option.Quest = GameId(0, 0x5A6DC);
    option.Text = acpText;
    return option;
}

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
        session.Outbox.clear();
        return cOpen[0]->Conversation;
    }

    void Options(ConnectionId_t aFrom, uint32_t aStep, DialogueOptions aOptions, bool aChoices = false)
    {
        DialogueOptionsRequest options;
        options.Conversation = Id;
        options.Step = aStep;
        options.Choices = aChoices;
        options.Options = std::move(aOptions);
        session.Receive(aFrom, options);
    }

    // A pick from that game's menu, which joined it.
    void Pick(ConnectionId_t aFrom, uint32_t aStep, uint32_t aTopic, GameId aInfo = {}, const char* acpText = "")
    {
        DialogueJoinRequest join;
        join.Conversation = Id;
        session.Receive(aFrom, join);
        DialoguePickRequest pick;
        pick.Conversation = Id;
        pick.Step = aStep;
        pick.Topic = GameId(0, aTopic);
        pick.Info = aInfo;
        pick.Text = acpText;
        session.Receive(aFrom, pick);
    }

    // The texts of the last list sent to that game.
    Vector<String> Texts(ConnectionId_t aTo)
    {
        Vector<String> texts;
        for (const auto& cOption : LastList(aTo).Options)
            texts.push_back(cOption.Text);
        return texts;
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

const DialogueOptions kHostOptions = {Option(0xA, 0xA1, "Follow me."), Option(0xB, 0xB1, "What news?")};
} // namespace

TEST_CASE("Dialogue: a peer talking to a copy starts the host's conversation", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: the peer who asked is in the conversation from the start", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: the host talking itself puts nobody in it", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: only the host's NPCs talk", "[Bridge][Dialogue]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);

    session.Receive(kOtherPeer, Start(cPeerPlayer));
    session.Receive(kHost, Start(cPeerPlayer));
    session.Receive(kOtherPeer, Start(0x7777));
    CHECK(session.SentTo<NotifyDialogueStart>(kHost).empty());
}

TEST_CASE("Dialogue: the host's menu opening opens it for everyone", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: a peer cannot say the host's menu opened", "[Bridge][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);

    DialogueOpenedRequest opened;
    opened.NpcId = cNpc;
    session.Receive(kPeer, opened);
    CHECK(session.SentTo<NotifyDialogueOpen>(kOtherPeer).empty());
}

TEST_CASE("Dialogue: one conversation at a time", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: talking to the NPC of the conversation joins it, no new start", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;

    session.Receive(kOtherPeer, Start(talking.Npc));
    CHECK(session.SentTo<NotifyDialogueStart>(kHost).empty());
    CHECK(session.SentTo<NotifyChatMessageBroadcast>(kOtherPeer).empty());
}

TEST_CASE("Dialogue: a player leaving leaves it on for the others; the last one out ends it", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: leaving twice, or without joining, ends nothing", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: the host's conversation ending ends it for everyone", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: only the host's game ends it outright", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kHost, Join(talking.Id));

    session.Receive(kPeer, Close(talking.Id, true));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
}

TEST_CASE("Dialogue: a close for an old conversation is dropped", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: a player who disconnects leaves it", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    session.Receive(kPeer, Join(talking.Id));

    session.Leave(kPeer);
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).size() == 1);
}

TEST_CASE("Dialogue: the host talking to another NPC ends the first conversation", "[Bridge][Dialogue]")
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

TEST_CASE("Dialogue: the host's options are the step's list, for every game", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.Options(kHost, 1, kHostOptions);
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto& cList = talking.LastList(cTo);
        CHECK(cList.Conversation == talking.Id);
        CHECK(cList.Step == 1);
        CHECK(!cList.Choices);
        CHECK(cList.Options == kHostOptions);
    }

    // A line's choices say so; a peer's game sends no list.
    session.Outbox.clear();
    talking.Options(kHost, 1, {Option(0xE, 0xE1, "Yes."), Option(0xF, 0xF1, "No.")}, true);
    CHECK(talking.LastList(kPeer).Choices);
    session.Outbox.clear();
    session.Receive(kPeer, Join(talking.Id));
    talking.Options(kPeer, 1, {Option(0xC, 0xC1, "For the Companions.")});
    CHECK(session.SentTo<NotifyDialogueList>(kOtherPeer).empty());
}

TEST_CASE("Dialogue: a list for another step or conversation is dropped", "[Bridge][Dialogue]")
{
    Talking talking;

    talking.Options(kHost, 2, kHostOptions);
    CHECK(talking.session.SentTo<NotifyDialogueList>(kHost).empty());
    talking.Id += 1;
    talking.Options(kHost, 1, kHostOptions);
    CHECK(talking.session.SentTo<NotifyDialogueList>(kHost).empty());
}

TEST_CASE("Dialogue: the first pick of a step wins, and every game hears it", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.Options(kHost, 1, kHostOptions);
    session.Outbox.clear();

    talking.Pick(kPeer, 1, 0xA, GameId(0, 0xA1));
    talking.Pick(kOtherPeer, 1, 0xB, GameId(0, 0xB1));
    talking.Pick(kHost, 1, 0xB, GameId(0, 0xB1));
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

TEST_CASE("Dialogue: only a player in the conversation picks", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.Options(kHost, 1, kHostOptions);
    session.Outbox.clear();

    // Far away, it has the list but no menu.
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

TEST_CASE("Dialogue: a pick says the picker's own line, else the host's", "[Bridge][Dialogue]")
{
    // B is a Speech check the host's player fails (B1) and the peer's passes (B2). C is a topic the host's player
    // does not get at all.
    const auto pick = [](ConnectionId_t aPicker, uint32_t aTopic, GameId aInfo) {
        Talking talking;
        talking.Options(kHost, 1, {Option(0xA, 0xA1, "Follow me."), Option(0xB, 0xB1, "(Persuade)")});
        talking.Pick(aPicker, 1, aTopic, aInfo);
        const auto cPicked = talking.session.SentTo<NotifyDialoguePicked>(kOtherPeer);
        return cPicked.empty() ? std::optional<GameId>{} : std::optional(cPicked[0]->Info);
    };

    CHECK(pick(kPeer, 0xB, GameId(0, 0xB2)) == GameId(0, 0xB2));
    CHECK(pick(kPeer, 0xC, GameId(0, 0xC1)) == GameId(0, 0xC1));
    // No line of its own for the topic: the host's; for a topic the host lacks too, nothing to say.
    CHECK(pick(kPeer, 0xB, {}) == GameId(0, 0xB1));
    CHECK(pick(kPeer, 0xC, {}) == std::nullopt);
}

TEST_CASE("Dialogue: a pick shows in everyone's chat as who said what", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.Options(kHost, 1, kHostOptions);
    session.Outbox.clear();

    talking.Pick(kPeer, 1, 0xC, GameId(0, 0xC1), "Here. Have a gold piece.");
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto cLines = session.SentTo<NotifyChatMessageBroadcast>(cTo);
        REQUIRE(cLines.size() == 1);
        CHECK(cLines[0]->MessageType == kPlayerDialogue);
        CHECK(cLines[0]->PlayerName == "Lydia");
        CHECK(cLines[0]->ChatMessage == "Here. Have a gold piece.");
    }
}

TEST_CASE("Dialogue: after a pick, the next list waits for the host's game", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.Options(kHost, 1, {Option(0xA, 0xA1, "Follow me.")});
    talking.Pick(kHost, 1, 0xA, GameId(0, 0xA1));
    session.Outbox.clear();

    // While the NPC answers: a late host list for the step picked is dropped, a pick for it too.
    talking.Options(kHost, 1, {Option(0xA, 0xA1, "Follow me.")});
    talking.Pick(kOtherPeer, 1, 0xA, GameId(0, 0xA1));
    // Before the host's next list, nothing to fall back on: a pick without a line of its own is dropped.
    talking.Pick(kOtherPeer, 2, 0xE);
    CHECK(session.SentTo<NotifyDialogueList>(kHost).empty());
    CHECK(session.SentTo<NotifyDialoguePicked>(kHost).empty());

    talking.Options(kHost, 2, {Option(0xE, 0xE1, "Wait here.")});
    CHECK(talking.LastList(kPeer).Step == 2);
    CHECK(talking.Texts(kPeer) == Vector<String>{"Wait here."});
}

TEST_CASE("Dialogue: Speech experience from a line goes to whoever picked it", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.Options(kHost, 1, kHostOptions);
    const auto experience = [&session](ConnectionId_t aFrom) {
        session.Outbox.clear();
        DialogueExperienceRequest request;
        request.Experience = 7.2f;
        session.Receive(aFrom, request);
    };
    const auto gained = [&session](ConnectionId_t aTo) {
        const auto cSent = session.SentTo<NotifyDialogueExperience>(aTo);
        return cSent.size() == 1 && cSent[0]->Experience == 7.2f;
    };

    // The peer's pick: its player's, nobody else's.
    talking.Pick(kPeer, 1, 0xA, GameId(0, 0xA1));
    experience(kHost);
    CHECK(gained(kPeer));
    CHECK(session.SentTo<NotifyDialogueExperience>(kHost).empty());
    CHECK(session.SentTo<NotifyDialogueExperience>(kOtherPeer).empty());

    // Only the host's game says lines.
    experience(kOtherPeer);
    CHECK(session.SentTo<NotifyDialogueExperience>(kPeer).empty());

    // The picker gone: the host's player keeps it.
    session.Leave(kPeer);
    experience(kHost);
    CHECK(gained(kHost));
}

TEST_CASE("Dialogue: a start that will not happen is refused to the asker", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    const auto refused = [&session](ConnectionId_t aTo, uint32_t aNpc) {
        const auto cRefused = session.SentTo<NotifyDialogueRefused>(aTo);
        return cRefused.size() == 1 && cRefused[0]->NpcId == aNpc;
    };

    // Someone else is talking: the asker hears why.
    const uint32_t cOtherNpc = session.Character(kHost);
    session.Receive(kPeer, Start(cOtherNpc));
    CHECK(refused(kPeer, cOtherNpc));
    CHECK(session.SentTo<NotifyChatMessageBroadcast>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyDialogueRefused>(kOtherPeer).empty());

    // Not the host's NPC (another player): refused without a line.
    session.Outbox.clear();
    const uint32_t cPlayer = session.Character(kOtherPeer);
    session.Receive(kPeer, Start(cPlayer));
    CHECK(refused(kPeer, cPlayer));
    CHECK(session.SentTo<NotifyChatMessageBroadcast>(kPeer).empty());
}

TEST_CASE("Dialogue: the host's game failing to start refuses the asker, who is not in the next one", "[Bridge][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);
    session.Receive(kPeer, Start(cNpc));

    // Only the host's game can say so.
    DialogueStartFailedRequest failed;
    failed.NpcId = cNpc;
    session.Receive(kOtherPeer, failed);
    CHECK(session.SentTo<NotifyDialogueRefused>(kPeer).empty());

    session.Receive(kHost, failed);
    const auto cRefused = session.SentTo<NotifyDialogueRefused>(kPeer);
    REQUIRE(cRefused.size() == 1);
    CHECK(cRefused[0]->NpcId == cNpc);
    const auto cLines = session.SentTo<NotifyChatMessageBroadcast>(kPeer);
    REQUIRE(cLines.size() == 1);
    CHECK(cLines[0]->MessageType == kSystemMessage);

    // The host talks to that NPC later: the refused peer is not in it, so its exit ends nothing.
    DialogueOpenedRequest opened;
    opened.NpcId = cNpc;
    session.Receive(kHost, opened);
    const uint32_t cId = session.SentTo<NotifyDialogueOpen>(kPeer).at(0)->Conversation;
    session.Receive(kHost, Join(cId));
    session.Receive(kPeer, Close(cId));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
}

TEST_CASE("Dialogue: a pick that is not said gets the picker the current list again", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.Options(kHost, 1, kHostOptions);
    talking.Pick(kPeer, 1, 0xA, GameId(0, 0xA1));

    // Picked as the list changed: while the NPC answers, nothing (the next list comes).
    session.Outbox.clear();
    talking.Pick(kOtherPeer, 1, 0xB, GameId(0, 0xB1));
    CHECK(session.SentTo<NotifyDialogueList>(kOtherPeer).empty());

    // Once the next list is there, a pick on the old one gets it, the picker only.
    talking.Options(kHost, 2, {Option(0xE, 0xE1, "Wait here.")}, true);
    session.Outbox.clear();
    talking.Pick(kOtherPeer, 1, 0xB, GameId(0, 0xB1));
    REQUIRE(session.SentTo<NotifyDialogueList>(kOtherPeer).size() == 1);
    CHECK(talking.LastList(kOtherPeer).Step == 2);
    CHECK(talking.LastList(kOtherPeer).Choices);
    CHECK(talking.Texts(kOtherPeer) == Vector<String>{"Wait here."});
    CHECK(session.SentTo<NotifyDialogueList>(kPeer).empty());
    CHECK(session.SentTo<NotifyDialoguePicked>(kPeer).empty());

    // A topic with nothing to say for it, the same.
    session.Outbox.clear();
    talking.Pick(kPeer, 2, 0xF);
    CHECK(session.SentTo<NotifyDialogueList>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyDialoguePicked>(kPeer).empty());
}

TEST_CASE("Dialogue: a training menu opens on the picker's game", "[Bridge][Dialogue]")
{
    Talking talking;
    auto& session = talking.session;
    talking.Options(kHost, 1, kHostOptions);
    talking.Pick(kPeer, 1, 0xA, GameId(0, 0xA1));
    session.Outbox.clear();
    DialogueTrainingRequest training;
    training.NpcId = talking.Npc;

    // Only the host's game runs the line's script.
    session.Receive(kOtherPeer, training);
    CHECK(session.SentTo<NotifyDialogueTraining>(kPeer).empty());

    session.Receive(kHost, training);
    const auto cTraining = session.SentTo<NotifyDialogueTraining>(kPeer);
    REQUIRE(cTraining.size() == 1);
    CHECK(cTraining[0]->NpcId == talking.Npc);
    CHECK(session.SentTo<NotifyDialogueTraining>(kHost).empty());
    CHECK(session.SentTo<NotifyDialogueTraining>(kOtherPeer).empty());

    // The picker gone: the host's player gets it.
    session.Leave(kPeer);
    session.Outbox.clear();
    session.Receive(kHost, training);
    CHECK(session.SentTo<NotifyDialogueTraining>(kHost).size() == 1);
}

TEST_CASE("Dialogue: an NPC greeting the host's player opens it for every player", "[Bridge][Dialogue]")
{
    Session session;
    const uint32_t cNpc = session.Character(kHost);
    DialogueOpenedRequest opened;
    opened.NpcId = cNpc;
    opened.Everyone = true;
    session.Receive(kHost, opened);
    for (const auto cTo : {kHost, kPeer, kOtherPeer})
    {
        const auto cOpen = session.SentTo<NotifyDialogueOpen>(cTo);
        REQUIRE(cOpen.size() == 1);
        CHECK(cOpen[0]->Everyone);
    }
    const uint32_t cId = session.SentTo<NotifyDialogueOpen>(kPeer).at(0)->Conversation;

    // Every peer is in it from the open: it ends only once both have left (the host's menu never joined).
    session.Receive(kPeer, Close(cId));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).empty());
    session.Receive(kOtherPeer, Close(cId));
    CHECK(session.SentTo<NotifyDialogueClosed>(kHost).size() == 1);
}

TEST_CASE("Dialogue: a choice the host's player gets no line for is a peer's to say with its own", "[Bridge][Dialogue]")
{
    // B is a persuade answer only the peer's Speech passes: the host's game lists it without a line.
    const DialogueOptions cChoices = {Option(0xA, 0xA1, "Yes."), Option(0xB, 0, "")};
    {
        Talking talking;
        talking.Options(kHost, 1, cChoices, true);
        talking.Pick(kPeer, 1, 0xB, GameId(0, 0xB2));
        const auto cPicked = talking.session.SentTo<NotifyDialoguePicked>(kHost);
        REQUIRE(cPicked.size() == 1);
        CHECK(cPicked[0]->Info == GameId(0, 0xB2));
    }
    {
        // Without a line of its own (the host's player picked it from an old list): nothing to say, the list back.
        Talking talking;
        talking.Options(kHost, 1, cChoices, true);
        talking.session.Outbox.clear();
        talking.Pick(kOtherPeer, 1, 0xB);
        CHECK(talking.session.SentTo<NotifyDialoguePicked>(kHost).empty());
        CHECK(talking.session.SentTo<NotifyDialogueList>(kOtherPeer).size() == 1);
    }
}
