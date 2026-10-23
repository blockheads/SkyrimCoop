#include <Services/DialogueService.h>

#include <Components.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/SharedDialogue.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>
#include <FormIds.h>

DialogueService::DialogueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                 IDialogueBridge& aBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_bridge(aBridge)
{
    Listen<UpdateEvent, &DialogueService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &DialogueService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<NotifyDialogueOpen, &DialogueService::OnOpen>(m_connections, aDispatcher, this);
    Listen<NotifyDialoguePicked, &DialogueService::OnPicked>(m_connections, aDispatcher, this);
    Listen<NotifyDialogueClosed, &DialogueService::OnClosed>(m_connections, aDispatcher, this);
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
        .List = [this]() { return m_pRole ? m_pRole->OnListBuilt() : std::nullopt; },
        .LineFor = [this](uint32_t aSpeakerFormId) { return m_pRole ? m_pRole->LineFor(aSpeakerFormId) : 0; },
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

void DialogueService::OnUpdate(const UpdateEvent&) noexcept
{
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
    const uint32_t cSpeaker = cOpen || m_pRole->RunsWithoutMenu() ? m_bridge.GetSpeaker() : 0;

    // In it while this game's menu is open on its NPC; the one who asked, from the open until its menu shows (or its
    // start came to nothing).
    const bool cIn = cOpen && m_conversation && Utils::GetSessionIdByFormId(cSpeaker) == m_npcSessionId;
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
    else if (!cOpen && !m_opening && m_joined && m_conversation)
    {
        DialogueCloseRequest close;
        close.Conversation = m_conversation;
        m_transport.Send(close);
    }
    m_joined = cIn || (m_joined && (cOpen || m_opening));

    m_menuOpen = cOpen;
    m_closing = m_closing && cOpen;
    m_pRole->OnUpdate(cOpen, cSpeaker);
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
}

void DialogueService::OnOpen(const NotifyDialogueOpen& acMessage) noexcept
{
    m_conversation = acMessage.Conversation;
    m_npcSessionId = acMessage.NpcId;
    m_step = 1;
    m_joined = false;
    m_opening = false;
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
    const bool cSwallow = m_pRole->SaysPicksElsewhere();
    const auto cOptions = m_bridge.GetOptions();
    if (aIndex >= cOptions.size())
    {
        spdlog::warn("Dialogue: a click on option {} of a list of {}", aIndex, cOptions.size());
        return cSwallow;
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
    m_pRole->OnPickSent();
    return cSwallow;
}

void DialogueService::OnPicked(const NotifyDialoguePicked& acMessage) noexcept
{
    if (acMessage.Conversation != m_conversation)
        return;
    m_step = acMessage.Step;
    const bool cOwnPick = acMessage.PlayerId == m_transport.GetLocalPlayerId();
    if (m_pRole)
        m_pRole->OnPicked(acMessage, ToGame(acMessage.Topic), ToGame(acMessage.Info), cOwnPick);
}

void DialogueService::OnClosed(const NotifyDialogueClosed& acMessage) noexcept
{
    if (acMessage.Conversation != m_conversation)
        return;
    m_conversation = 0;
    m_joined = false;
    m_opening = false;
    if (m_pRole)
        m_pRole->OnClosed();
    if (m_bridge.IsMenuOpen())
        CloseMenu();
}
