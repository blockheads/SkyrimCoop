#include <Services/Host/HostDialogue.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/Relayed.h>
#include <Messages/SharedDialogue.h>
#include <Services/PartyOptionText.h>

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
    aAuthority.On(*this, &HostDialogue::OnHighlight);
}

DialogueOptions MergeDialogueLists(const DialogueOptions& acBase, const std::vector<PartyDialogueList>& acMembers)
{
    DialogueOptions merged;
    const auto cAdd = [&](const DialogueOption& acOption) {
        if (DialogueLineFor(acOption.Topic, merged))
            return;
        DialogueOption option = acOption;
        option.Players.clear();
        for (const auto& cMember : acMembers)
            if (DialogueLineFor(acOption.Topic, *cMember.pOptions))
                option.Players.push_back(cMember.PlayerId);
        if (option.Players.empty())
            return; // nobody in it has this one
        if (option.Players.size() == acMembers.size())
            option.Players.clear(); // everyone's
        merged.push_back(std::move(option));
    };
    for (const auto& cOption : acBase)
        cAdd(cOption);
    for (const auto& cMember : acMembers)
        for (const auto& cOption : *cMember.pOptions)
            cAdd(cOption);
    return merged;
}

const DialogueOption* DialogueLineFor(const GameId& acTopic, const DialogueOptions& acOwn)
{
    const auto cLine = std::ranges::find(acOwn, acTopic, &DialogueOption::Topic);
    return cLine != acOwn.end() ? &*cLine : nullptr;
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
    open.Greeting = acRequest.Greeting;
    m_players.SendToAll(open);
}

void HostDialogue::OnOptions(const DialogueOptionsRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    if (!pConversation || aSender != m_players.Host())
        return;
    pConversation->Options = acRequest.Options;
    pConversation->PlayerOptions.clear();
    for (const auto& cPlayer : acRequest.Players)
        pConversation->PlayerOptions[cPlayer.PlayerId] = cPlayer.Options;
    pConversation->Choices = acRequest.Choices;
    Merge();
}

void HostDialogue::OnPick(const DialoguePickRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    // A pick that is not said (another player picked first, the list not shown yet, not its own option) gets the
    // picker's menu the current list again; one of another player's options also gets its chat the reason (its game
    // told it already: this is the backstop).
    const bool cListed = pConversation && pConversation->Options && DialogueLineFor(acRequest.Topic, pConversation->Merged);
    const DialogueOptions* pOwn = pConversation ? OptionsFor(aSender) : nullptr;
    const DialogueOption* pLine = cListed && pOwn && pConversation->Has(aSender) ? DialogueLineFor(acRequest.Topic, *pOwn) : nullptr;
    if (!pLine)
    {
        spdlog::info("HostDialogue: {:x}'s pick of {:X} at step {} is not said ({})", aSender, acRequest.Topic.BaseId,
                     acRequest.Step, !pConversation ? "another step or conversation" : !pConversation->Options ? "no list yet"
                     : !pConversation->Has(aSender) ? "not in it" : !cListed ? "not in the party's list" : "not its own option");
        if (Conversation* pOpen = Current(acRequest.Conversation); pOpen && pOpen->Has(aSender))
            SendList(aSender);
        if (cListed && pConversation->Has(aSender))
        {
            NotifyChatMessageBroadcast line;
            line.MessageType = kSystemMessage;
            line.ChatMessage = ChatText(CannotSayText(m_players.PlayerNameOf(aSender)));
            m_network.Send(aSender, line);
        }
        return;
    }
    // Copied: the step's lists go below.
    const GameId cInfo = pLine->Info;
    const std::string cText = pLine->Text;

    NotifyDialoguePicked picked;
    picked.Conversation = pConversation->Id;
    picked.Step = pConversation->Step + 1;
    picked.Topic = acRequest.Topic;
    picked.Info = cInfo;
    picked.PlayerId = m_players.PlayerIdOf(aSender);

    // The next step's list is the host's game's once the NPC has answered.
    pConversation->Step = picked.Step;
    pConversation->Options.reset();
    pConversation->PlayerOptions.clear();
    pConversation->Merged.clear();

    // Every game, the host's too: the host's game says the line.
    m_players.SendToAll(picked);

    // Who said what, in everyone's chat ("Lydia: I found Lucan's claw.").
    NotifyChatMessageBroadcast line;
    line.MessageType = kPlayerChat;
    line.PlayerName = ChatText(m_players.PlayerNameOf(aSender));
    line.ChatMessage = ChatText(cText);
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
        Merge();
    }
}

void HostDialogue::OnHighlight(const DialogueHighlightRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    if (!pConversation || !pConversation->Has(aSender))
        return;
    NotifyDialogueHighlight moved;
    static_cast<Payloads::DialogueHighlight&>(moved) = acRequest;
    moved.PlayerId = m_players.PlayerIdOf(aSender);
    for (const ConnectionId_t cPlayer : pConversation->Players)
        if (cPlayer != aSender)
            m_network.Send(cPlayer, moved);
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

const DialogueOptions* HostDialogue::OptionsFor(ConnectionId_t aPlayer) const noexcept
{
    if (!m_conversation || !m_conversation->Options)
        return nullptr;
    const auto it = m_conversation->PlayerOptions.find(m_players.PlayerIdOf(aPlayer));
    return it != m_conversation->PlayerOptions.end() ? &it->second : &*m_conversation->Options;
}

std::vector<PartyDialogueList> HostDialogue::Members() const noexcept
{
    std::vector<PartyDialogueList> members;
    for (const ConnectionId_t cPlayer : m_conversation->Players)
        if (const DialogueOptions* pOptions = OptionsFor(cPlayer))
            members.push_back({m_players.PlayerIdOf(cPlayer), pOptions});
    return members;
}

void HostDialogue::Merge() noexcept
{
    if (!m_conversation->Options)
        return;
    m_conversation->Merged = MergeDialogueLists(*m_conversation->Options, Members());
    for (const ConnectionId_t cPlayer : m_conversation->Players)
        SendList(cPlayer);
}

void HostDialogue::SendList(ConnectionId_t aPlayer) noexcept
{
    if (!m_conversation || !m_conversation->Options)
        return;
    NotifyDialogueList list;
    list.Conversation = m_conversation->Id;
    list.Step = m_conversation->Step;
    list.Choices = m_conversation->Choices;
    list.Options = m_conversation->Merged;
    // A topic it has shows its own line ("I have your claw." to the holder, "About your claw..." to the others).
    if (const DialogueOptions* pOwn = OptionsFor(aPlayer))
        for (DialogueOption& option : list.Options)
            if (const DialogueOption* pOwnLine = DialogueLineFor(option.Topic, *pOwn))
            {
                option.Info = pOwnLine->Info;
                option.Quest = pOwnLine->Quest;
                option.Text = pOwnLine->Text;
            }
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
    if (!std::erase(m_conversation->Players, aPlayer))
        return;
    if (m_conversation->Players.empty())
        End();
    else
        Merge(); // its options leave the party's list
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
