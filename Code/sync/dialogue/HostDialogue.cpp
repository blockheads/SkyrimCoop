#include <dialogue/HostDialogue.h>

#include <core/HostNetwork.h>
#include <core/HostChat.h>
#include <core/HostRouter.h>
#include <characters/HostCharacters.h>
#include <core/HostPlayers.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/Relayed.h>
#include <Messages/SharedDialogue.h>
#include <dialogue/PartyOptionText.h>

#include <algorithm>

HostDialogue::HostDialogue(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers,
                           const HostCharacters& acCharacters) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_characters(acCharacters)
{
    aRouter.Relay<DialogueRequest, NotifyDialogue>(HostRouter::Delivery::FromAnyone);
    aRouter.Relay<SubtitleRequest, NotifySubtitle>(HostRouter::Delivery::FromAnyone);
    aRouter.On(*this, &HostDialogue::OnStart);
    aRouter.On(*this, &HostDialogue::OnOpened);
    aRouter.On(*this, &HostDialogue::OnOptions);
    aRouter.On(*this, &HostDialogue::OnPick);
    aRouter.On(*this, &HostDialogue::OnClose);
    aRouter.On(*this, &HostDialogue::OnJoin);
    aRouter.On(*this, &HostDialogue::OnStartFailed);
    aRouter.On(*this, &HostDialogue::OnHighlight);
    aRouter.On(*this, &HostDialogue::OnSkip);
}

DialogueOptions MergeDialogueLists(const DialogueOptions& acBase, const std::vector<PartyDialogueList>& acMembers)
{
    DialogueOptions merged;
    const auto cAdd = [&](const DialogueOption& acOption) {
        if (SameOptionIn(acOption, merged))
            return;
        DialogueOption option = acOption;
        option.Players.clear();
        for (const auto& cMember : acMembers)
            if (SameOptionIn(acOption, *cMember.pOptions))
                option.Players.push_back(cMember.PlayerId);
        if (option.Players.empty())
            return; // nobody in it has this one
        if (option.Players.size() == acMembers.size())
            option.Players.clear(); // everyone's
        merged.push_back(std::move(option));
    };
    // Each of the host's topics in its order, with every line of it the players have (Lucan's claw: one each).
    for (const auto& cOption : acBase)
    {
        cAdd(cOption);
        for (const auto& cMember : acMembers)
            for (const auto& cOwn : *cMember.pOptions)
                if (cOwn.Topic == cOption.Topic)
                    cAdd(cOwn);
    }
    for (const auto& cMember : acMembers)
        for (const auto& cOption : *cMember.pOptions)
            cAdd(cOption);
    return merged;
}

const DialogueOption* SameOptionIn(const DialogueOption& acOption, const DialogueOptions& acList)
{
    const auto cSame = std::ranges::find_if(acList, [&](const DialogueOption& acListed) {
        return acListed.Topic == acOption.Topic && (acListed.Info == acOption.Info || acListed.Text == acOption.Text);
    });
    return cSame != acList.end() ? &*cSame : nullptr;
}

const DialogueOption* DialogueLineFor(const GameId& acTopic, const GameId& acInfo, const DialogueOptions& acOwn)
{
    const auto cLine = std::ranges::find_if(acOwn, [&](const DialogueOption& acOption) {
        return acOption.Topic == acTopic && acOption.Info == acInfo;
    });
    return cLine != acOwn.end() ? &*cLine : nullptr;
}

void HostDialogue::OnStart(const DialogueStartRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    // Only the host's NPCs talk: a player's character (the host's own too) is nobody to start a conversation with.
    if (!cHost || m_characters.OwnerOf(acRequest.NpcId) != cHost || m_characters.IsPlayerCharacter(acRequest.NpcId))
    {
        spdlog::debug("HostDialogue: {:x} asked to talk to {:x}, not the host's", aSender, acRequest.NpcId);
        Refuse(aSender, acRequest.NpcId, nullptr);
        return;
    }
    // Talking to the NPC of the conversation under way joins it: the asker is told, at its step.
    if (m_conversation)
    {
        if (m_conversation->NpcId != acRequest.NpcId)
        {
            Refuse(aSender, acRequest.NpcId, "Someone else is in a conversation already.");
            return;
        }
        if (!m_conversation->Has(aSender))
            m_conversation->Players.push_back(aSender);
        spdlog::debug("HostDialogue: {:x} asks for {:x}, already in conversation {}: it joins", aSender, acRequest.NpcId, m_conversation->Id);
        SendOpen(aSender);
        Merge();
        return;
    }

    // The host's game starts it; its conversation opening opens it for everyone, the asker in it (OnOpened).
    // One start at a time: another ask for the same NPC joins it, one for another NPC is refused.
    if (m_asked)
    {
        if (m_asked->NpcId != acRequest.NpcId)
            Refuse(aSender, acRequest.NpcId, "Someone else is in a conversation already.");
        else if (std::ranges::find(m_asked->Players, aSender) == m_asked->Players.end())
            m_asked->Players.push_back(aSender);
        spdlog::debug("HostDialogue: {:x} asks to talk to {:x} while {:x} is being started", aSender, acRequest.NpcId, m_asked->NpcId);
        return;
    }
    spdlog::debug("HostDialogue: {:x} asks to talk to {:x}", aSender, acRequest.NpcId);
    m_abandoned.reset();
    m_asked = Asked{{aSender}, acRequest.NpcId};
    NotifyDialogueStart start;
    start.NpcId = acRequest.NpcId;
    start.PlayerId = m_players.PlayerIdOf(aSender);
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
    m_conversation->Greeting = acRequest.Greeting;
    if (!acRequest.Everyone && std::exchange(m_abandoned, std::nullopt) == acRequest.NpcId)
    {
        spdlog::debug("HostDialogue: conversation {} with {:x} opens with its askers gone: it ends", m_conversation->Id, acRequest.NpcId);
        End();
        return;
    }
    // Asking for it is joining it, and an NPC greeting the party puts every other player in it: each game leaves it
    // if its menu does not open.
    if (acRequest.Everyone)
    {
        for (const ConnectionId_t cPlayer : m_players.Connections())
            if (cPlayer != aSender)
                m_conversation->Players.push_back(cPlayer);
    }
    else if (m_asked && m_asked->NpcId == acRequest.NpcId)
        m_conversation->Players = m_asked->Players;
    m_conversation->Facing = m_asked && m_asked->NpcId == acRequest.NpcId ? m_asked->Players.front() : aSender;
    spdlog::debug("HostDialogue: conversation {} with {:x} opens, {} player(s) in it", m_conversation->Id, acRequest.NpcId,
                  m_conversation->Players.size());
    m_asked.reset();

    m_conversation->Everyone = acRequest.Everyone;
    for (const ConnectionId_t cPlayer : m_players.Connections())
        SendOpen(cPlayer);
}

void HostDialogue::SendOpen(ConnectionId_t aPlayer) const noexcept
{
    NotifyDialogueOpen open;
    open.Conversation = m_conversation->Id;
    open.NpcId = m_conversation->NpcId;
    open.Everyone = m_conversation->Everyone;
    open.InIt = m_conversation->Has(aPlayer);
    open.Greeting = m_conversation->Greeting;
    open.Step = m_conversation->Step;
    open.Picked = m_conversation->Picked;
    m_network.Send(aPlayer, open);
}

void HostDialogue::OnOptions(const DialogueOptionsRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    if (!pConversation || aSender != m_players.Host())
        return;
    pConversation->Options = acRequest.Options;
    pConversation->Picked.clear();
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
    const DialogueOption* pListed = pConversation && pConversation->Options
                                        ? DialogueLineFor(acRequest.Topic, acRequest.Info, pConversation->Merged) : nullptr;
    const bool cListed = pListed != nullptr;
    const DialogueOptions* pOwn = pConversation ? OptionsFor(aSender) : nullptr;
    // The picker's own line of that option: the listed one, or its own of the same text (a topic's random lines).
    const DialogueOption* pLine = cListed && pOwn && pConversation->Has(aSender) ? SameOptionIn(*pListed, *pOwn) : nullptr;
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
    picked.Text = acRequest.Text;

    // The next step's list is the host's game's once the NPC has answered.
    pConversation->Step = picked.Step;
    pConversation->Picked = picked.Text;
    pConversation->Facing = aSender;
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
        spdlog::debug("HostDialogue: {:x} joins conversation {} ({} in it)", aSender, pConversation->Id, pConversation->Players.size());
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
    // To every menu, the mover's too: the order moves reach the host is the one every menu ends on.
    for (const ConnectionId_t cPlayer : pConversation->Players)
        m_network.Send(cPlayer, moved);
}

void HostDialogue::OnSkip(const DialogueSkipRequest& acRequest, ConnectionId_t aSender) noexcept
{
    Conversation* pConversation = Current(acRequest.Conversation, acRequest.Step);
    if (!pConversation || !pConversation->Has(aSender))
        return;
    NotifyDialogueSkip skip;
    static_cast<Payloads::DialogueSkip&>(skip) = acRequest;
    // A host that pressed has moved on already; every copy of the NPC goes quiet with it, heard from anywhere.
    m_players.SendToOthers(skip, aSender == m_players.Host() ? aSender : 0);
}

void HostDialogue::OnStartFailed(const DialogueStartFailedRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (aSender != m_players.Host() || !m_asked || m_asked->NpcId != acRequest.NpcId)
        return;
    spdlog::debug("HostDialogue: the host's game did not start {:x}: {} ask(s) refused", acRequest.NpcId, m_asked->Players.size());
    for (const ConnectionId_t cPlayer : m_asked->Players)
        Refuse(cPlayer, acRequest.NpcId, acRequest.NothingToSay ? "They have nothing to say." : "The host can't talk to them right now.");
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
    m_network.Send(aPlayer, list);
}

void HostDialogue::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    if (m_asked && std::erase(m_asked->Players, aPlayer) && m_asked->Players.empty())
    {
        m_abandoned = m_asked->NpcId;
        m_asked.reset();
    }
    if (m_conversation)
        Leave(aPlayer);
}

void HostDialogue::SendCurrent(ConnectionId_t aJoiner) const noexcept
{
    if (m_conversation)
        SendOpen(aJoiner);
}

void HostDialogue::Leave(ConnectionId_t aPlayer) noexcept
{
    // The host's game is in it without being listed: its player leaving ends nothing.
    const bool cListed = std::erase(m_conversation->Players, aPlayer) != 0;
    spdlog::debug("HostDialogue: {:x} leaves conversation {} ({}, {} left in it)", aPlayer, m_conversation->Id, cListed ? "listed" : "not listed",
                  m_conversation->Players.size());
    if (cListed && m_conversation->Players.empty())
    {
        End();
        return;
    }
    if (m_conversation->Facing == aPlayer && !m_conversation->Players.empty())
        Face(m_conversation->Players.front());
    if (cListed)
        Merge(); // its options leave the party's list
}

void HostDialogue::Face(ConnectionId_t aPlayer) noexcept
{
    m_conversation->Facing = aPlayer;
    NotifyDialogueFace face;
    face.Conversation = m_conversation->Id;
    face.PlayerId = m_players.PlayerIdOf(aPlayer);
    m_network.Send(m_players.Host(), face);
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
