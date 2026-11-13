#include <Services/DialogueService.h>
#include <Services/DialogueMenuPolicy.h>

#include <Components.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/MenuEvent.h>
#include <Events/SessionNoticeEvent.h>
#include <Messages/Relayed.h>
#include <Messages/SharedDialogue.h>
#include <Services/PartyOptionText.h>
#include <Services/SessionService.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>
#include <FormIds.h>
#include <Diagnostics/Profile.h>

DialogueService::DialogueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                 IDialogueBridge& aBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_bridge(aBridge)
{
    Listen<MenuEvent, &DialogueService::OnMenu>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &DialogueService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &DialogueService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<NotifyDialogueOpen, &DialogueService::OnOpen>(m_connections, aDispatcher, this);
    Listen<NotifyDialoguePicked, &DialogueService::OnPicked>(m_connections, aDispatcher, this);
    Listen<NotifyDialogueClosed, &DialogueService::OnClosed>(m_connections, aDispatcher, this);
    Listen<NotifyDialogueList, &DialogueService::OnList>(m_connections, aDispatcher, this);
    Listen<NotifyDialogueHighlight, &DialogueService::OnHighlight>(m_connections, aDispatcher, this);
    Listen<NotifyDialogueSkip, &DialogueService::OnSkip>(m_connections, aDispatcher, this);
}

DialogueService::~DialogueService() noexcept
{
    m_bridge.SetFilters({});
}

void DialogueService::SetRole(Role* apRole) noexcept
{
    m_pRole = apRole;
    if (!m_pRole)
    {
        m_bridge.SetFilters({});
        m_bridge.SetMenuCloseEnds(true);
        return;
    }
    m_bridge.SetFilters({
        .Start = [this](uint32_t aActorFormId, bool aForceGreet) { return !m_pRole || m_pRole->OnStartAsked(aActorFormId, aForceGreet); },
        .Talk = [this](uint32_t aActorFormId) { return m_pRole && m_transport.IsOnline() && m_pRole->OnTalk(aActorFormId); },
        .Pick = [this](uint32_t aIndex) { return OnPickAsked(aIndex); },
        .Skip = [this]() { OnSkipAsked(); },
        .List = [this]() { return m_pRole ? m_pRole->OnListBuilt() : std::nullopt; },
        .LineFor = [this](uint32_t aSpeakerFormId) { return m_pRole ? m_pRole->LineFor(aSpeakerFormId) : 0; },
        .LookAt = [this](uint32_t aSpeakerFormId) { return m_pRole ? m_pRole->LookAt(aSpeakerFormId) : 0; },
        .Judged = [this]() { return m_pRole ? m_pRole->Judged() : std::vector<uint32_t>{}; },
    });
}

void DialogueService::Opening() noexcept
{
    m_joined = true;
    m_opening = true;
    m_openingFrames = 0;
}

void DialogueService::Restart() noexcept
{
    m_conversation = 0;
    m_joined = false;
}

void DialogueService::CloseMenu() noexcept
{
    if (std::exchange(m_closing, true))
        return;
    m_bridge.CloseMenu();
}

std::optional<uint32_t> DialogueService::CopySessionId(uint32_t aFormId) const noexcept
{
    if (!aFormId)
        return std::nullopt;
    auto view = m_world.view<FormIdComponent, RemoteComponent>();
    for (const entt::entity cEntity : view)
        if (view.get<FormIdComponent>(cEntity).Id == aFormId)
            return view.get<RemoteComponent>(cEntity).Id;
    return std::nullopt;
}

std::optional<std::vector<IDialogueBridge::Option>> DialogueService::PartyList() const noexcept
{
    if (!m_conversation || !m_listStep || m_listStep != m_step)
        return std::nullopt;
    return m_list;
}

void DialogueService::OnList(const NotifyDialogueList& acMessage) noexcept
{
    if (acMessage.Conversation != m_conversation)
        return;
    m_listStep = acMessage.Step;
    m_listChoices = acMessage.Choices;
    if (m_listStep == m_step)
        m_pickedText.clear();
    m_list.clear();
    m_listPlayers.clear();
    const auto& cSession = m_world.GetSessionService();
    for (const auto& cOption : acMessage.Options)
    {
        m_listPlayers.push_back(cOption.Players);
        std::vector<std::string> names;
        for (const uint32_t cPlayer : cOption.Players)
            names.push_back(cSession.NameOf(cPlayer));
        m_list.push_back({ToGame(cOption.Topic), ToGame(cOption.Info), cOption.Quest != GameId{} ? ToGame(cOption.Quest) : 0,
                          PartyOptionText(cOption.Text, names)});
    }
    // The same step again (a pick that was not said, a player joining or leaving): its menu shows it again.
    m_shownStep = 0;
}

void DialogueService::ShowPartyList() noexcept
{
    if (!m_listStep || m_listStep != m_step || m_shownStep == m_step)
        return;
    if (!m_bridge.ShowList(m_list, m_listChoices, m_resuming))
        return;
    m_shownStep = m_step;
    m_resuming = false;
    m_highlight.reset();
}

void DialogueService::OnHighlight(const NotifyDialogueHighlight& acMessage) noexcept
{
    if (acMessage.Conversation != m_conversation)
        return;
    // This menu's own move back from the host: an older one would pull the highlight back.
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId() && acMessage.Seq != m_highlightSeq)
        return;
    m_pendingHighlight = std::pair{acMessage.Step, acMessage.Index};
}

void DialogueService::OnSkipAsked() noexcept
{
    if (!m_transport.IsOnline() || !m_conversation || !m_joined)
        return;
    DialogueSkipRequest skip;
    skip.Conversation = m_conversation;
    skip.Step = m_step;
    m_transport.Send(skip);
}

void DialogueService::OnSkip(const NotifyDialogueSkip& acMessage) noexcept
{
    if (m_pRole && acMessage.Conversation == m_conversation && acMessage.Step == m_step)
        m_pRole->OnLineSkipped();
}

void DialogueService::SyncHighlight() noexcept
{
    if (m_shownStep != m_step)
        return;
    // This menu's own move first: the host echoes every move to every menu, so a crossing one settles in its order.
    if (const auto cHighlight = m_bridge.GetHighlight(); cHighlight && cHighlight != m_highlight)
    {
        const bool cFirst = !m_highlight;
        m_highlight = cHighlight;
        // A new list's first highlight (the menu's fill, 51515) moved nobody's: told only when it is not the top.
        if (!cFirst || *cHighlight != 0)
        {
            m_pendingHighlight.reset();
            SendHighlight(*cHighlight);
            return;
        }
    }
    if (!m_pendingHighlight)
        return;
    const auto [cStep, cIndex] = *m_pendingHighlight;
    if (cStep != m_step)
    {
        if (cStep < m_step)
            m_pendingHighlight.reset();
        return;
    }
    m_pendingHighlight.reset();
    m_bridge.SetHighlight(cIndex);
    m_highlight = cIndex;
}

void DialogueService::SendHighlight(uint32_t aIndex) noexcept
{
    DialogueHighlightRequest moved;
    moved.Conversation = m_conversation;
    moved.Step = m_step;
    moved.Index = aIndex;
    moved.Seq = ++m_highlightSeq;
    m_transport.Send(moved);
}

void DialogueService::RestoreList(uint32_t aIndex) noexcept
{
    if (!PartyList() || aIndex >= m_list.size())
        return;
    m_shownStep = 0;
    m_pendingHighlight = std::pair{m_step, aIndex};
    SendHighlight(aIndex);
}

void DialogueService::OnMenu(const MenuEvent& acEvent) noexcept
{
    if (acEvent.Menu != "BarterMenu" || !m_pRole || !m_joined || !m_conversation)
        return;
    m_bartering = acEvent.Opening;
    m_bridge.SetMenuCloseEnds(DialogueMenuPolicy::CloseEndsConversation(
        m_pRole->RunsWithoutMenu(), m_bartering, m_conversation != 0));
    if (m_bartering)
    {
        m_resuming = false;
        CloseMenu();
    }
    else if (m_bridge.GetSpeaker())
    {
        m_resuming = true;
        m_shownStep = 0;
        m_bridge.ShowMenu();
        Opening();
    }
}

void DialogueService::OnUpdate(const UpdateEvent&) noexcept
{
    PROFILE_ZONE("DialogueService::OnUpdate");
    if (!m_transport.IsOnline() || !m_pRole)
        return;

    // Its speaker deleted while talking (the copy of an NPC left behind on travel): over, and its menu closes.
    if (m_bridge.ClearOrphaned())
    {
        spdlog::info("Dialogue: the speaker of this game's conversation is gone, cleared");
        if (m_bridge.IsMenuOpen())
            CloseMenu();
    }

    const bool cOpen = m_bridge.IsMenuOpen();
    const uint32_t cSpeaker = cOpen || m_bartering || m_pRole->RunsWithoutMenu() ? m_bridge.GetSpeaker() : 0;

    // In it while this game's menu is open on its NPC; the one who asked, from the open until its menu shows (or its
    // start came to nothing).
    const bool cIn = (cOpen || m_bartering) && m_conversation && Utils::GetSessionIdByFormId(cSpeaker) == m_npcSessionId;
    if (m_opening && (cOpen || m_bridge.GetSpeaker() != Utils::GetFormIdBySessionId(m_npcSessionId)))
        m_opening = false;
    // A menu the UI never opened (its show dropped): the copy never ends its conversation, so this game leaves.
    constexpr uint32_t cMenuFrames = 60;
    if (m_opening && ++m_openingFrames > cMenuFrames)
    {
        spdlog::warn("Dialogue: the menu of conversation {} did not open, leaving it", m_conversation);
        m_opening = false;
        m_bridge.End();
    }
    if (cIn && !m_joined)
    {
        DialogueJoinRequest join;
        join.Conversation = m_conversation;
        m_transport.Send(join);
    }
    else if (!cOpen && !m_bartering && !m_opening && m_joined && m_conversation)
    {
        DialogueCloseRequest close;
        close.Conversation = m_conversation;
        m_transport.Send(close);
    }
    m_joined = cIn || (m_joined && (cOpen || m_bartering || m_opening));

    // A menu that closes shows the pick again when it reopens in the same step (a new SWF on the engine's list).
    if (m_menuOpen && !cOpen)
        m_pickedShownStep = 0;
    m_menuOpen = cOpen;
    m_closing = m_closing && cOpen;
    m_pRole->OnUpdate(cOpen, cSpeaker);
    if (cOpen && m_joined)
    {
        ShowPartyList();
        // Joined while the NPC answers: the pick, as every other menu shows it, once this one shows a list.
        if (!PartyList() && !m_pickedText.empty() && m_pickedShownStep != m_step && m_bridge.ShowPicked(m_pickedText))
        {
            spdlog::info("Dialogue: joined while the NPC answers step {}: the menu shows the pick", m_step);
            m_pickedShownStep = m_step;
        }
        SyncHighlight();
    }
}

void DialogueService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_conversation = 0;
    m_npcSessionId = 0;
    m_step = 0;
    m_joined = false;
    m_opening = false;
    m_menuOpen = false;
    m_closing = false;
    m_bartering = false;
    m_resuming = false;
    m_listStep = 0;
    m_shownStep = 0;
    m_highlight.reset();
    m_pendingHighlight.reset();
    m_pickedText.clear();
    m_pickedShownStep = 0;
}

void DialogueService::OnOpen(const NotifyDialogueOpen& acMessage) noexcept
{
    m_conversation = acMessage.Conversation;
    m_resuming = false;
    m_npcSessionId = acMessage.NpcId;
    m_step = 1;
    m_joined = false;
    m_opening = false;
    m_listStep = 0;
    m_shownStep = 0;
    m_highlight.reset();
    m_pendingHighlight.reset();
    m_pickedText.clear();
    m_pickedShownStep = 0;
    if (m_pRole)
        m_pRole->OnOpen(acMessage);
}

bool DialogueService::OnPickAsked(uint32_t aIndex) noexcept
{
    if (!m_transport.IsOnline() || !m_conversation || !m_joined || !m_pRole)
    {
        spdlog::info("Dialogue: a click on option {} is not a pick (conversation {}, joined {})", aIndex, m_conversation, m_joined);
        return false;
    }
    // Never said by this menu: the host's rule names who says it, and the host's game says that player's line.
    const auto cOptions = m_bridge.GetOptions();
    if (aIndex >= cOptions.size())
    {
        spdlog::warn("Dialogue: a click on option {} of a list of {}", aIndex, cOptions.size());
        RestoreList(m_highlight.value_or(0));
        return true;
    }
    // Another player's option: this player may highlight it, not say it; only its chat hears why.
    if (m_shownStep == m_step)
        if (const auto cListed = std::ranges::find_if(m_list, [&](const IDialogueBridge::Option& acListed) {
                return acListed.Topic == cOptions[aIndex].Topic && acListed.Info == cOptions[aIndex].Info;
            });
            cListed != m_list.end())
            if (const uint32_t cMe = m_transport.GetLocalPlayerId(); !CanSay(m_listPlayers[cListed - m_list.begin()], cMe))
            {
                spdlog::info("Dialogue: option {} (topic {:X}) is another player's: not sent", aIndex, cOptions[aIndex].Topic);
                RestoreList(aIndex);
                m_world.GetDispatcher().trigger(SessionNoticeEvent{CannotSayText(m_world.GetSessionService().NameOf(cMe))});
                return true;
            }
    spdlog::info("Dialogue: picks option {} of step {}: topic {:X}, line {:X}", aIndex, m_step, cOptions[aIndex].Topic, cOptions[aIndex].Info);
    // Its menu's option, as the host's game listed it.
    DialoguePickRequest pick;
    pick.Conversation = m_conversation;
    pick.Step = m_step;
    pick.Topic = ToSession(cOptions[aIndex].Topic).value_or(GameId{});
    pick.Info = ToSession(cOptions[aIndex].Info).value_or(GameId{});
    pick.Text = cOptions[aIndex].Text;
    m_transport.Send(pick);
    return true;
}

void DialogueService::OnPicked(const NotifyDialoguePicked& acMessage) noexcept
{
    if (acMessage.Conversation != m_conversation)
        return;
    m_step = acMessage.Step;
    const uint32_t cTopic = ToGame(acMessage.Topic), cInfo = ToGame(acMessage.Info);
    const auto cPicked = std::ranges::find_if(m_list, [&](const IDialogueBridge::Option& acListed) {
        return acListed.Topic == cTopic && acListed.Info == cInfo;
    });
    // The picker's own line of a shared option (a topic's random lines) is not listed: the topic's then.
    const auto cTopicLine = std::ranges::find(m_list, cTopic, &IDialogueBridge::Option::Topic);
    // Not in this menu's list (it joined mid-line): as the picker's menu showed it.
    m_pickedText = cPicked != m_list.end() ? cPicked->Text : cTopicLine != m_list.end() ? cTopicLine->Text : acMessage.Text;
    // Every menu in it shows the pick, as the picker's does after its click, until the next list.
    if (m_menuOpen && m_joined && m_bridge.ShowPicked(m_pickedText))
        m_pickedShownStep = m_step;
    const bool cOwnLine = acMessage.PlayerId == m_transport.GetLocalPlayerId();
    if (m_pRole)
        m_pRole->OnPicked(acMessage, ToGame(acMessage.Topic), ToGame(acMessage.Info), cOwnLine);
}

void DialogueService::OnClosed(const NotifyDialogueClosed& acMessage) noexcept
{
    if (acMessage.Conversation != m_conversation)
        return;
    m_conversation = 0;
    m_joined = false;
    m_bartering = false;
    m_resuming = false;
    m_opening = false;
    m_pickedText.clear();
    m_pickedShownStep = 0;
    if (m_pRole)
        m_pRole->OnClosed();
    if (m_bridge.IsMenuOpen())
        CloseMenu();
}
