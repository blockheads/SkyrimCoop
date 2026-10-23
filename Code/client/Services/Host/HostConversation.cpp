#include <Services/Host/HostConversation.h>

#include <Messages/SharedDialogue.h>
#include <Components.h>
#include <Services/RunnerService.h>
#include <Services/TransportService.h>
#include <Utils.h>
#include <World.h>

HostConversation::HostConversation(World& aWorld, entt::dispatcher& aDispatcher, DialogueService& aDialogue) noexcept
    : m_world(aWorld)
    , m_dialogue(aDialogue)
{
    m_startConnection = aDispatcher.sink<NotifyDialogueStart>().connect<&HostConversation::OnStart>(this);
    m_dialogue.SetRole(this);
}

HostConversation::~HostConversation() noexcept
{
    m_dialogue.SetRole(nullptr);
}

bool HostConversation::OnStartAsked(uint32_t aActorFormId, bool aForceGreet) noexcept
{
    // One conversation at a time: while its own runs without the menu, no other starts. An NPC greeting this game's
    // player (its AI runs here only) greets the party: every game opens its menu.
    auto& bridge = m_dialogue.Bridge();
    const uint32_t cSpeaker = bridge.GetSpeaker();
    if (!m_dialogue.Conversation() || !cSpeaker || bridge.IsMenuOpen() || aActorFormId == cSpeaker)
    {
        m_greeter = aForceGreet ? aActorFormId : 0;
        return true;
    }
    spdlog::info("Dialogue: {:X} would talk to the host, but the party talks to {:X}", aActorFormId, cSpeaker);
    return false;
}

bool HostConversation::OnTalk(uint32_t aActorFormId) noexcept
{
    // The conversation runs without its menu: talking to its NPC opens it.
    auto& bridge = m_dialogue.Bridge();
    if (bridge.IsMenuOpen() || !m_dialogue.Conversation() || aActorFormId != bridge.GetSpeaker())
        return false;
    spdlog::info("Dialogue: the host joins conversation {} with {:X}", m_dialogue.Conversation(), aActorFormId);
    if (!m_menuAsked)
        m_menuAsked = bridge.ShowMenu();
    return true;
}

uint32_t HostConversation::LineFor(uint32_t aSpeakerFormId) noexcept
{
    return aSpeakerFormId == m_pickerSpeaker ? m_picker.load() : 0;
}

void HostConversation::OnStart(const NotifyDialogueStart& acMessage) noexcept
{
    auto& bridge = m_dialogue.Bridge();
    DialogueStartFailedRequest failed;
    failed.NpcId = acMessage.NpcId;
    // Busy with a conversation of its own (one at a time).
    if (bridge.GetSpeaker())
    {
        m_dialogue.Transport().Send(failed);
        return;
    }
    const uint32_t cFormId = Utils::GetFormIdBySessionId(acMessage.NpcId);
    if (!cFormId || m_dialogue.CopySessionId(cFormId))
    {
        spdlog::warn("Dialogue: a guest asked to talk to {:X}, which is not the host's here", acMessage.NpcId);
        m_dialogue.Transport().Send(failed);
        return;
    }
    // This game's player is the one talking, wherever it stands, without its menu: it joins by talking to the NPC.
    spdlog::info("Dialogue: a guest started a conversation with {:X}, run in the background", cFormId);
    if (!bridge.Start(cFormId, false))
    {
        spdlog::info("Dialogue: the engine did not start the conversation with {:X}", cFormId);
        m_dialogue.Transport().Send(failed);
    }
}

std::optional<std::vector<IDialogueBridge::Option>> HostConversation::OnListBuilt() noexcept
{
    m_world.GetRunner().Queue([this] {
        if (std::exchange(m_awaitingList, false))
            SendOptions();
    });
    return std::nullopt;
}

void HostConversation::OnOpen(const NotifyDialogueOpen&) noexcept
{
    m_picker = 0;
    m_awaitingList = false;
    SendOptions();
}

void HostConversation::OnPicked(const NotifyDialoguePicked& acMessage, uint32_t aTopic, uint32_t aInfo, bool aOwnPick) noexcept
{
    auto& bridge = m_dialogue.Bridge();
    // Before its line begins: its script runs for the picker.
    m_pickerSpeaker = bridge.GetSpeaker();
    m_picker = aOwnPick ? 0x14 : CharacterOf(acMessage.PlayerId); // the host's own pick: its player's (0x14)
    m_awaitingList = true;
    // Its own menu said its own pick.
    if (aOwnPick || !bridge.GetSpeaker())
        return;
    if (!bridge.PickOption(aTopic, aInfo))
    {
        // Nothing said: its list as it stands is the next step's.
        spdlog::warn("Dialogue: picked topic {:X} with info {:X} is not in the list here", aTopic, aInfo);
        m_awaitingList = false;
        SendOptions();
        return;
    }
    spdlog::info("Dialogue: player {} picked topic {:X} (info {:X})", acMessage.PlayerId, aTopic, aInfo);
}

void HostConversation::OnClosed() noexcept
{
    auto& bridge = m_dialogue.Bridge();
    bridge.SetMenuCloseEnds(true);
    if (!bridge.IsMenuOpen() && bridge.GetSpeaker())
        bridge.End();
}

void HostConversation::OnUpdate(bool aMenuOpen, uint32_t aSpeaker) noexcept
{
    auto& transport = m_dialogue.Transport();
    // Opened, or this game's player turned to another NPC: a new conversation for everyone.
    if (aSpeaker && aSpeaker != m_speaker)
    {
        m_dialogue.Restart();
        if (const auto cSessionId = Utils::GetSessionIdByFormId(aSpeaker); cSessionId && !m_dialogue.CopySessionId(aSpeaker))
        {
            DialogueOpenedRequest opened;
            opened.NpcId = *cSessionId;
            opened.Everyone = aSpeaker == m_greeter;
            transport.Send(opened);
        }
    }
    // Its conversation ended (a goodbye, the NPC walking off, combat): it ends for everyone.
    if (!aSpeaker && m_speaker && m_dialogue.Conversation())
    {
        spdlog::info("Dialogue: the host's engine ended conversation {}", m_dialogue.Conversation());
        DialogueCloseRequest close;
        close.Conversation = m_dialogue.Conversation();
        close.Ended = true;
        transport.Send(close);
        m_dialogue.Restart();
    }
    // Its player leaving the menu leaves the conversation, which the host's rule ends once nobody is in it.
    m_dialogue.Bridge().SetMenuCloseEnds(!m_dialogue.Conversation());
    m_menuAsked = m_menuAsked && aSpeaker && !aMenuOpen;
    m_speaker = aSpeaker;
}

uint32_t HostConversation::CharacterOf(uint32_t aPlayerId) const noexcept
{
    auto view = m_world.view<PlayerComponent, FormIdComponent>();
    for (const entt::entity cEntity : view)
        if (view.get<PlayerComponent>(cEntity).Id == aPlayerId)
            return view.get<FormIdComponent>(cEntity).Id;
    return 0;
}

void HostConversation::SendOptions() noexcept
{
    auto& bridge = m_dialogue.Bridge();
    if (!m_dialogue.Conversation() || !bridge.GetSpeaker())
        return;

    auto& modSystem = m_world.GetModSystem();
    DialogueOptionsRequest request;
    request.Conversation = m_dialogue.Conversation();
    request.Step = m_dialogue.Step();
    request.Choices = bridge.IsChoices();
    for (const auto& cOption : bridge.GetOptions())
    {
        DialogueOption& option = request.Options.emplace_back();
        modSystem.GetServerModId(cOption.Topic, option.Topic);
        modSystem.GetServerModId(cOption.Info, option.Info);
        if (cOption.Quest)
            modSystem.GetServerModId(cOption.Quest, option.Quest);
        option.Text = cOption.Text;
    }
    spdlog::info("Dialogue: step {} of conversation {}: {} options{}", request.Step, request.Conversation,
                 request.Options.size(), request.Choices ? ", a line's choices" : "");
    m_dialogue.Transport().Send(request);
}
