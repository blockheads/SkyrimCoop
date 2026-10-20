#include <Services/Host/HostDialogue.h>

#include <GameServer.h>
#include <Services/Host/HostBridge.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/SharedDialogue.h>

#include <algorithm>

HostDialogue::HostDialogue(HostBridge& aBridge, Server::GameServer& aServer, const HostPlayers& acPlayers,
                           const HostCharacters& acCharacters) noexcept
    : m_server(aServer)
    , m_players(acPlayers)
    , m_characters(acCharacters)
{
    aBridge.On(*this, &HostDialogue::OnStart);
    aBridge.On(*this, &HostDialogue::OnOpened);
    aBridge.On(*this, &HostDialogue::OnOptions);
    aBridge.On(*this, &HostDialogue::OnPick);
    aBridge.On(*this, &HostDialogue::OnClose);
    aBridge.On(*this, &HostDialogue::OnJoin);
    aBridge.On(*this, &HostDialogue::OnExperience);
    aBridge.On(*this, &HostDialogue::OnStartFailed);
    aBridge.On(*this, &HostDialogue::OnTraining);
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
    pConversation->HostOptions = acRequest.Options;
    pConversation->Choices = acRequest.Choices;
    for (const ConnectionId_t cPlayer : m_players.Connections())
        SendList(cPlayer);
}

void HostDialogue::OnPick(const DialoguePickRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    // A pick that is not said (another player picked first, nothing to say for it): the picker's menu shows the
    // current list again. While the NPC answers, the next list comes anyway.
    if (!pConversation || !pConversation->HostOptions || !pConversation->Has(aSender))
    {
        if (Conversation* pOpen = Current(acRequest.Conversation); pOpen && pOpen->Has(aSender))
            SendList(aSender);
        return;
    }
    // The picker's own line for the topic (its Speech, its perks, its race), else the host's: a topic of its own
    // that the host lacks comes with its line.
    GameId info = acRequest.Info;
    if (!info)
    {
        const auto& cOptions = *pConversation->HostOptions;
        const auto cHost = std::ranges::find(cOptions, acRequest.Topic, &DialogueOption::Topic);
        // Not the host's, or a choice its player gets no line for: nothing to say.
        if (cHost == cOptions.end() || !cHost->Info)
        {
            SendList(aSender);
            return;
        }
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
    m_server.Send(Talker(), experience);
}

void HostDialogue::OnStartFailed(const DialogueStartFailedRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (aSender != m_players.Host() || !m_asked || m_asked->NpcId != acRequest.NpcId)
        return;
    Refuse(m_asked->Player, acRequest.NpcId, "The host can't talk to them right now.");
    m_asked.reset();
}

void HostDialogue::OnTraining(const DialogueTrainingRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (aSender != m_players.Host())
    {
        spdlog::warn("HostDialogue: {:x} is not the host and sent a training menu", aSender);
        return;
    }
    // The picker trains on its own game: its gold, its skills.
    NotifyDialogueTraining training;
    training.NpcId = acRequest.NpcId;
    m_server.Send(Talker(), training);
}

void HostDialogue::Refuse(ConnectionId_t aPlayer, uint32_t aNpcId, const char* acpWhy) noexcept
{
    NotifyDialogueRefused refused;
    refused.NpcId = aNpcId;
    m_server.Send(aPlayer, refused);
    if (!acpWhy)
        return;
    NotifyChatMessageBroadcast line;
    line.MessageType = kSystemMessage;
    line.ChatMessage = acpWhy;
    m_server.Send(aPlayer, line);
}

void HostDialogue::SendList(ConnectionId_t aPlayer) noexcept
{
    if (!m_conversation || !m_conversation->HostOptions)
        return;
    NotifyDialogueList list;
    list.Conversation = m_conversation->Id;
    list.Step = m_conversation->Step;
    list.Choices = m_conversation->Choices;
    list.Options = *m_conversation->HostOptions;
    m_server.Send(aPlayer, list);
}

HostDialogue::ConnectionId_t HostDialogue::Talker() const noexcept
{
    return m_talker && m_players.IsPlayer(m_talker) ? m_talker : m_players.Host();
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
