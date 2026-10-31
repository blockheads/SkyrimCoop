#include <Services/Guest/GuestDialogue.h>

#include <Bridges/IActorBridge.h>

#include <Messages/Relayed.h>
#include <Messages/SharedDialogue.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>
#include <FormIds.h>

GuestDialogue::GuestDialogue(World& aWorld, entt::dispatcher& aDispatcher, DialogueService& aDialogue) noexcept
    : m_world(aWorld)
    , m_dialogue(aDialogue)
{
    Listen<NotifyDialogueRefused, &GuestDialogue::OnRefused>(m_connections, aDispatcher, this);
    Listen<NotifyDialogue, &GuestDialogue::OnLine>(m_connections, aDispatcher, this);
    Listen<NotifySubtitle, &GuestDialogue::OnSubtitle>(m_connections, aDispatcher, this);
    m_dialogue.SetRole(this);
}

GuestDialogue::~GuestDialogue() noexcept
{
    m_dialogue.SetRole(nullptr);
}

bool GuestDialogue::OnStartAsked(uint32_t aActorFormId, bool) noexcept
{
    // A copy's conversation is the host's: one under way with it is joined (OnTalk), any other is asked of the host,
    // whose game talks to the real NPC.
    const auto cSessionId = m_dialogue.CopySessionId(aActorFormId);
    if (!cSessionId)
        return true;
    if (m_dialogue.Conversation() && *cSessionId == m_dialogue.NpcSessionId())
        return true;
    spdlog::info("Dialogue: asking the host to talk to {:X} (copy {:X})", *cSessionId, aActorFormId);
    m_asked = *cSessionId;
    DialogueStartRequest request;
    request.NpcId = *cSessionId;
    m_dialogue.Transport().Send(request);
    return false;
}

bool GuestDialogue::OnTalk(uint32_t aActorFormId) noexcept
{
    // Joins with its menu on its copy of the NPC.
    if (m_dialogue.Bridge().IsMenuOpen() || !m_dialogue.Conversation() || m_dialogue.CopySessionId(aActorFormId) != m_dialogue.NpcSessionId())
        return false;
    spdlog::info("Dialogue: joining conversation {} (copy {:X})", m_dialogue.Conversation(), aActorFormId);
    m_dialogue.Bridge().Start(aActorFormId, true, m_greeting);
    return true;
}

void GuestDialogue::OnOpen(const NotifyDialogueOpen& acMessage) noexcept
{
    m_greeting = ToGame(acMessage.Greeting);
    // The player who asked for it is in it, every player when the NPC greeted the party: its menu opens on its copy,
    // or it leaves.
    if (std::exchange(m_asked, 0) != acMessage.NpcId && !acMessage.Everyone)
        return;
    auto& bridge = m_dialogue.Bridge();
    const uint32_t cCopy = Utils::GetFormIdBySessionId(acMessage.NpcId);
    if (cCopy && bridge.Start(cCopy, true, m_greeting))
    {
        spdlog::info("Dialogue: conversation {} opens on copy {:X}", acMessage.Conversation, cCopy);
        m_dialogue.Opening();
        return;
    }
    spdlog::warn("Dialogue: conversation {} could not open on copy {:X}", acMessage.Conversation, cCopy);
    if (cCopy && bridge.GetSpeaker() == cCopy)
        bridge.End();
    DialogueCloseRequest close;
    close.Conversation = acMessage.Conversation;
    m_dialogue.Transport().Send(close);
}

void GuestDialogue::OnClosed() noexcept
{
    m_greeting = 0;
    auto& bridge = m_dialogue.Bridge();
    bridge.SetMenuCloseEnds(true);
    if (!bridge.IsMenuOpen() && bridge.GetSpeaker())
        bridge.End();
}

std::optional<std::vector<IDialogueBridge::Option>> GuestDialogue::OnListBuilt() noexcept
{
    // In a conversation its menu shows only the party's list: until the step's arrives, its engine's show is held.
    if (auto list = m_dialogue.PartyList())
        return list;
    return m_dialogue.Conversation() ? std::optional(std::vector<IDialogueBridge::Option>{}) : std::nullopt;
}

void GuestDialogue::OnRefused(const NotifyDialogueRefused& acMessage) noexcept
{
    if (m_asked == acMessage.NpcId)
        m_asked = 0;
}

void GuestDialogue::OnUpdate(bool aMenuOpen, uint32_t aSpeaker) noexcept
{
    // A menu on a copy opens only for a conversation; one that ended before it opened closes.
    if (aMenuOpen && !m_dialogue.Conversation() && m_dialogue.CopySessionId(aSpeaker))
        m_dialogue.CloseMenu();
    else if (aMenuOpen && !m_menuOpen && m_dialogue.Conversation())
        ReplayGreeting();
    m_menuOpen = aMenuOpen;
}

void GuestDialogue::OnLine(const NotifyDialogue& acMessage) noexcept
{
    m_lastLine = {acMessage.SessionId, acMessage.SoundFilename, 0, std::chrono::steady_clock::now()};
}

void GuestDialogue::OnSubtitle(const NotifySubtitle& acMessage) noexcept
{
    m_lastSubtitle = {acMessage.SessionId, acMessage.Text, acMessage.TopicFormId, std::chrono::steady_clock::now()};
}

void GuestDialogue::ReplayGreeting() noexcept
{
    constexpr auto cRecent = std::chrono::seconds(3);
    const auto cNow = std::chrono::steady_clock::now();
    const uint32_t cNpc = m_dialogue.NpcSessionId();
    const uint32_t cCopy = Utils::GetFormIdBySessionId(cNpc);
    if (!cCopy)
        return;
    auto& actors = m_world.GetActorBridge();
    if (m_lastLine.SessionId == cNpc && cNow - m_lastLine.At < cRecent)
        actors.ActorSpeakSound(cCopy, m_lastLine.Text);
    if (m_lastSubtitle.SessionId == cNpc && cNow - m_lastSubtitle.At < cRecent)
        actors.ActorShowSubtitle(cCopy, m_lastSubtitle.Text, m_lastSubtitle.TopicInfo, false);
}
