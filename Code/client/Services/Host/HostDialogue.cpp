#include <Services/Host/HostDialogue.h>

#include <GameServer.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/SharedDialogue.h>

#include <algorithm>

HostDialogue::HostDialogue(HostAuthority& aAuthority, Server::GameServer& aServer, const HostPlayers& acPlayers,
                           const HostCharacters& acCharacters) noexcept
    : m_server(aServer)
    , m_players(acPlayers)
    , m_characters(acCharacters)
{
    aAuthority.On(*this, &HostDialogue::OnStart);
    aAuthority.On(*this, &HostDialogue::OnOpened);
    aAuthority.On(*this, &HostDialogue::OnOptions);
    aAuthority.On(*this, &HostDialogue::OnPick);
    aAuthority.On(*this, &HostDialogue::OnClose);
    aAuthority.On(*this, &HostDialogue::OnJoin);
    aAuthority.On(*this, &HostDialogue::OnExperience);
}

void HostDialogue::OnStart(const DialogueStartRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    if (!cHost || m_characters.OwnerOf(acRequest.NpcId) != cHost)
    {
        spdlog::debug("HostDialogue: {:x} asked to talk to {:x}, not the host's", aSender, acRequest.NpcId);
        return;
    }
    // Talking to the NPC of the conversation under way joins it (its game opens its menu itself).
    if (m_conversation)
    {
        if (m_conversation->NpcId != acRequest.NpcId)
        {
            NotifyChatMessageBroadcast line;
            line.MessageType = kSystemMessage;
            line.ChatMessage = "Someone else is in a conversation already.";
            m_server.Send(aSender, line);
        }
        return;
    }

    // The host's game starts it; its conversation opening opens it for everyone, the asker in it (OnOpened).
    m_asked = Asked{aSender, acRequest.NpcId};
    NotifyDialogueStart start;
    start.NpcId = acRequest.NpcId;
    m_server.Send(cHost, start);
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

    m_conversation.emplace();
    m_conversation->Id = ++m_lastId;
    m_conversation->NpcId = acRequest.NpcId;
    m_talker = 0;
    // Asking for it is joining it: its game leaves it if its menu does not open.
    if (m_asked && m_asked->NpcId == acRequest.NpcId)
        m_conversation->Players.push_back(m_asked->Player);
    m_asked.reset();

    NotifyDialogueOpen open;
    open.Conversation = m_conversation->Id;
    open.NpcId = acRequest.NpcId;
    m_players.SendToAll(open);
}

void HostDialogue::OnOptions(const DialogueOptionsRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    if (!pConversation || aSender != m_players.Host())
        return;
    pConversation->HostOptions = acRequest.Options;

    NotifyDialogueList list;
    list.Conversation = pConversation->Id;
    list.Step = pConversation->Step;
    list.Choices = acRequest.Choices;
    list.Options = acRequest.Options;
    m_players.SendToAll(list);
}

void HostDialogue::OnPick(const DialoguePickRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    if (!pConversation || !pConversation->HostOptions || !pConversation->Has(aSender))
        return;
    // The picker's own line for the topic (its Speech, its perks, its race), else the host's: a topic of its own
    // that the host lacks comes with its line.
    GameId info = acRequest.Info;
    if (!info)
    {
        const auto& cOptions = *pConversation->HostOptions;
        const auto cHost = std::ranges::find(cOptions, acRequest.Topic, &DialogueOption::Topic);
        if (cHost == cOptions.end())
            return;
        info = cHost->Info;
    }

    NotifyDialoguePicked picked;
    picked.Conversation = pConversation->Id;
    picked.Step = pConversation->Step + 1;
    picked.Topic = acRequest.Topic;
    picked.Info = info;
    picked.PlayerId = m_players.PlayerIdOf(aSender);

    // The next step's list is the host's game's once the NPC has answered.
    pConversation->Step = picked.Step;
    m_talker = aSender;
    pConversation->HostOptions.reset();

    // Every game, the host's too: its game says it unless its own menu did.
    m_players.SendToAll(picked);

    // Who said what, in everyone's chat ("Lydia: Here. Have a gold piece.").
    NotifyChatMessageBroadcast line;
    line.MessageType = kPlayerDialogue;
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
        pConversation->Players.push_back(aSender);
}

void HostDialogue::OnExperience(const DialogueExperienceRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    if (aSender != cHost)
    {
        spdlog::warn("HostDialogue: {:x} is not the host and sent dialogue experience", aSender);
        return;
    }
    // The last picker's; the host's own when it picked or that player left.
    NotifyDialogueExperience experience;
    experience.Experience = acRequest.Experience;
    m_server.Send(m_talker && m_players.IsPlayer(m_talker) ? m_talker : cHost, experience);
}

void HostDialogue::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    if (m_asked && m_asked->Player == aPlayer)
        m_asked.reset();
    if (m_talker == aPlayer)
        m_talker = 0;
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
