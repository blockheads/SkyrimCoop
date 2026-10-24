#include <Services/Host/HostDialogue.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/SharedDialogue.h>

#include <algorithm>

HostDialogue::HostDialogue(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers,
                           const HostCharacters& acCharacters) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_characters(acCharacters)
{
    aAuthority.On(*this, &HostDialogue::OnStart);
    aAuthority.On(*this, &HostDialogue::OnOpened);
    aAuthority.On(*this, &HostDialogue::OnOptions);
    aAuthority.On(*this, &HostDialogue::OnPick);
    aAuthority.On(*this, &HostDialogue::OnClose);
    aAuthority.On(*this, &HostDialogue::OnJoin);
    aAuthority.On(*this, &HostDialogue::OnStartFailed);
}

void HostDialogue::OnStart(const DialogueStartRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    if (!cHost || m_characters.OwnerOf(acRequest.NpcId) != cHost)
    {
        spdlog::debug("HostDialogue: {:x} asked to talk to {:x}, not the host's", aSender, acRequest.NpcId);
        Refuse(aSender, acRequest.NpcId, nullptr);
        return;
    }
    // Talking to the NPC of the conversation under way joins it (its game opens its menu itself).
    if (m_conversation)
    {
        if (m_conversation->NpcId != acRequest.NpcId)
            Refuse(aSender, acRequest.NpcId, "Someone else is in a conversation already.");
        return;
    }

    // The host's game starts it; its conversation opening opens it for everyone, the asker in it (OnOpened).
    m_asked = Asked{aSender, acRequest.NpcId};
    NotifyDialogueStart start;
    start.NpcId = acRequest.NpcId;
    m_network.Send(cHost, start);
}

void HostDialogue::OnOpened(const DialogueOpenedRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (aSender != m_players.Host())
    {
        spdlog::warn("HostDialogue: {:x} is not the host and sent a dialogue opening", aSender);
        return;
    }
    // The host's player turned to another NPC: the engine ended the first conversation.
    if (m_conversation)
        End(aSender);

    m_conversation = Conversation{};
    m_conversation->Id = ++m_lastId;
    m_conversation->NpcId = acRequest.NpcId;
    // Asking for it is joining it, and an NPC greeting the party puts every other player in it: each game leaves it
    // if its menu does not open.
    if (acRequest.Everyone)
    {
        for (const ConnectionId_t cPlayer : m_players.Connections())
            if (cPlayer != aSender)
                m_conversation->Players.push_back(cPlayer);
    }
    else if (m_asked && m_asked->NpcId == acRequest.NpcId)
        m_conversation->Players.push_back(m_asked->Player);
    m_asked.reset();

    NotifyDialogueOpen open;
    open.Conversation = m_conversation->Id;
    open.NpcId = acRequest.NpcId;
    open.Everyone = acRequest.Everyone;
    m_players.SendToAll(open);
}

void HostDialogue::OnOptions(const DialogueOptionsRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    if (!pConversation || aSender != m_players.Host())
        return;
    pConversation->Options = acRequest.Options;
    pConversation->Choices = acRequest.Choices;
    for (const ConnectionId_t cPlayer : pConversation->Players)
        SendList(cPlayer);
}

void HostDialogue::OnPick(const DialoguePickRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    // A pick that is not said (another player picked first, the list not shown yet, not one of its options): the
    // picker's menu shows the current list again. While the NPC answers, the next list comes anyway.
    const auto cIsListed = [&acRequest](const DialogueOption& acOption) {
        return acOption.Topic == acRequest.Topic && acOption.Info == acRequest.Info;
    };
    if (!pConversation || !pConversation->Options || !pConversation->Has(aSender) ||
        std::ranges::none_of(*pConversation->Options, cIsListed))
    {
        spdlog::info("HostDialogue: {:x}'s pick of {:X} at step {} is not said ({})", aSender, acRequest.Topic.BaseId,
                     acRequest.Step, !pConversation ? "another step or conversation" : !pConversation->Options ? "no list yet"
                     : !pConversation->Has(aSender) ? "not in it" : "not in the list");
        if (Conversation* pOpen = Current(acRequest.Conversation); pOpen && pOpen->Has(aSender))
            SendList(aSender);
        return;
    }
    const GameId info = acRequest.Info;

    NotifyDialoguePicked picked;
    picked.Conversation = pConversation->Id;
    picked.Step = pConversation->Step + 1;
    picked.Topic = acRequest.Topic;
    picked.Info = info;
    picked.PlayerId = m_players.PlayerIdOf(aSender);

    // The next step's list is the host's game's once the NPC has answered.
    pConversation->Step = picked.Step;
    pConversation->Options.reset();

    // Every game, the host's too: its game says it unless its own menu did.
    m_players.SendToAll(picked);

    // Who said what, in everyone's chat ("Lydia: Here. Have a gold piece.").
    NotifyChatMessageBroadcast line;
    line.MessageType = kPlayerChat;
    line.PlayerName = ChatText(m_players.PlayerNameOf(aSender));
    line.ChatMessage = ChatText(acRequest.Text);
    m_players.SendToAll(line);
}

void HostDialogue::OnClose(const DialogueCloseRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (!m_conversation)
        return;
    // The host's game is always in the current conversation: its end is the conversation's, sent before it learnt
    // the id too.
    if (aSender == m_players.Host() && acRequest.Ended)
        End(aSender);
    else if (Current(acRequest.Conversation))
        Leave(aSender);
}

void HostDialogue::OnJoin(const DialogueJoinRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (Conversation* pConversation = Current(acRequest.Conversation); pConversation && !pConversation->Has(aSender))
    {
        pConversation->Players.push_back(aSender);
        SendList(aSender);
    }
}

void HostDialogue::OnStartFailed(const DialogueStartFailedRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (aSender != m_players.Host() || !m_asked || m_asked->NpcId != acRequest.NpcId)
        return;
    Refuse(m_asked->Player, acRequest.NpcId, "The host can't talk to them right now.");
    m_asked.reset();
}

void HostDialogue::Refuse(ConnectionId_t aPlayer, uint32_t aNpcId, const char* acpWhy) noexcept
{
    NotifyDialogueRefused refused;
    refused.NpcId = aNpcId;
    m_network.Send(aPlayer, refused);
    if (!acpWhy)
        return;
    NotifyChatMessageBroadcast line;
    line.MessageType = kSystemMessage;
    line.ChatMessage = acpWhy;
    m_network.Send(aPlayer, line);
}

void HostDialogue::SendList(ConnectionId_t aPlayer) noexcept
{
    if (!m_conversation || !m_conversation->Options || aPlayer == m_players.Host())
        return;
    NotifyDialogueList list;
    list.Conversation = m_conversation->Id;
    list.Step = m_conversation->Step;
    list.Choices = m_conversation->Choices;
    list.Options = *m_conversation->Options;
    m_network.Send(aPlayer, list);
}

void HostDialogue::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    if (m_asked && m_asked->Player == aPlayer)
        m_asked.reset();
    if (m_conversation)
        Leave(aPlayer);
}

void HostDialogue::Leave(ConnectionId_t aPlayer) noexcept
{
    if (std::erase(m_conversation->Players, aPlayer) && m_conversation->Players.empty())
        End();
}

HostDialogue::Conversation* HostDialogue::Current(uint32_t aId) noexcept
{
    return m_conversation && m_conversation->Id == aId ? &*m_conversation : nullptr;
}

HostDialogue::Conversation* HostDialogue::Current(uint32_t aId, uint32_t aStep) noexcept
{
    Conversation* pConversation = Current(aId);
    return pConversation && pConversation->Step == aStep ? pConversation : nullptr;
}

void HostDialogue::End(ConnectionId_t aExcept) noexcept
{
    NotifyDialogueClosed closed;
    closed.Conversation = m_conversation->Id;
    m_conversation.reset();
    m_players.SendToOthers(closed, aExcept);
}
