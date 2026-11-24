#include <Services/Guest/GuestDialogue.h>
#include <Services/Guest/VoiceFiles.h>

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
    // The host says who is in it (who asked, everyone when the NPC greeted the party): its menu opens on its copy, or
    // it leaves. Another's conversation with the NPC it asked for is not the answer to its ask.
    if (!acMessage.InIt)
        return;
    const bool cOwnStart = std::exchange(m_asked, 0) == acMessage.NpcId || acMessage.Everyone;
    auto& bridge = m_dialogue.Bridge();
    const uint32_t cCopy = Utils::GetFormIdBySessionId(acMessage.NpcId);
    if (cCopy && bridge.Start(cCopy, true, m_greeting))
    {
        spdlog::info("Dialogue: conversation {} opens on copy {:X}", acMessage.Conversation, cCopy);
        m_replayGreeting = cOwnStart && acMessage.Step == 1; // joined later, the copy's line is not the greeting
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
    m_replayGreeting = false;
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

void GuestDialogue::OnLineSkipped() noexcept
{
    if (const uint32_t cCopy = Utils::GetFormIdBySessionId(m_dialogue.NpcSessionId()))
        m_world.GetActorBridge().ActorStopCurrentDialogue(cCopy);
}

void GuestDialogue::OnLeft() noexcept
{
    // Every NPC a guest talks to is a copy.
    // A speaker gone already leaves the menu open: cleared all the same, ClearOrphaned runs only online.
    auto& bridge = m_dialogue.Bridge();
    if (!bridge.GetSpeaker() && !bridge.IsMenuOpen())
        return;
    spdlog::info("Dialogue: leaving the session ends the conversation on copy {:X}", bridge.GetSpeaker());
    bridge.Clear();
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
    else if (aMenuOpen && !m_menuOpen && m_dialogue.Conversation() && m_replayGreeting)
        ReplayGreeting();
    // Joined while the NPC answers: its menu shows the answer's line, not its own engine's greeting text.
    else if (aMenuOpen && !m_menuOpen && m_dialogue.Conversation() && m_dialogue.Step() > 1 && !m_dialogue.PartyList() &&
             m_lastSubtitle.SessionId == m_dialogue.NpcSessionId())
        if (const uint32_t cCopy = Utils::GetFormIdBySessionId(m_dialogue.NpcSessionId()))
            m_world.GetActorBridge().ActorShowSubtitle(cCopy, m_lastSubtitle.Text, m_lastSubtitle.TopicInfo, false);
    if (aMenuOpen)
        m_replayGreeting = false;
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
    // Only the greeting's line: an answer said before the menu opened (a pick, or the host's own click) is the copy's
    // still, and a replay would layer it. Its subtitle goes with it (a subtitle names no info in Skyrim).
    if (m_lastLine.SessionId != cNpc || cNow - m_lastLine.At >= cRecent || !VoiceFiles::IsLineOf(m_lastLine.Text, m_greeting))
        return;
    spdlog::info("Dialogue: the menu opens: the NPC's last line again on copy {:X}", cCopy);
    actors.ActorSpeakSound(cCopy, m_lastLine.Text);
    if (m_lastSubtitle.SessionId == cNpc && cNow - m_lastSubtitle.At < cRecent)
        actors.ActorShowSubtitle(cCopy, m_lastSubtitle.Text, m_lastSubtitle.TopicInfo, false);
}
